#import "jucer_LoopbackServer.h"

#if TARGET_OS_IPHONE

#import <Network/Network.h>
#import <Security/Security.h>

@interface ServedResource : NSObject
@property (nonatomic, strong) NSData* data;
@property (nonatomic, copy) NSString* contentType;
@end

@implementation ServedResource
@end

@implementation LoopbackServer
{
    sec_identity_t identity;
    nw_listener_t listener;
    dispatch_queue_t queue;
    dispatch_semaphore_t readySemaphore;
    NSMutableDictionary<NSString*, ServedResource*>* resources;
    void (^log)(NSString*);
    BOOL listenerReady;
}

- (instancetype) initWithIdentityData: (NSData*) identityData
                           passphrase: (NSString*) passphrase
                               logger: (void (^)(NSString*)) logger
{
    if ((self = [super init]) == nil)
        return nil;

    log = [logger copy];
    queue = dispatch_queue_create ("pocselfinstall.server", DISPATCH_QUEUE_SERIAL);
    /*  JUCE は MRC。[NSMutableDictionary dictionary] だと init 後に
        プールが空いて辞書が消え、後で Network のオブジェクトが同じ番地に乗る。 */
    resources = [[NSMutableDictionary alloc] init];

    CFArrayRef imported = NULL;
    NSDictionary* options = @{ (__bridge id) kSecImportExportPassphrase: passphrase };
    const OSStatus status = SecPKCS12Import ((__bridge CFDataRef) identityData,
                                             (__bridge CFDictionaryRef) options,
                                             &imported);

    if (status != errSecSuccess || imported == NULL || CFArrayGetCount (imported) == 0)
    {
        log ([NSString stringWithFormat: @"SecPKCS12Import failed: OSStatus %d", (int) status]);

        if (imported != NULL)
            CFRelease (imported);

        return self;
    }

    NSDictionary* item = (__bridge NSDictionary*) CFArrayGetValueAtIndex (imported, 0);
    SecIdentityRef secIdentity = (__bridge SecIdentityRef) item[(__bridge id) kSecImportItemIdentity];

    if (secIdentity != NULL)
    {
        identity = sec_identity_create (secIdentity);
        log (@"identity loaded from PKCS#12");
    }
    else
    {
        log (@"PKCS#12 contained no identity");
    }

    CFRelease (imported);
    return self;
}

- (void) dealloc
{
    [self stop];
    identity = nil;
    queue = nil;
    readySemaphore = nil;
    [resources release];
    resources = nil;
    [log release];
    log = nil;
    [super dealloc];
}

- (void) serveData: (NSData*) data atPath: (NSString*) path contentType: (NSString*) contentType
{
    ServedResource* resource = [[ServedResource alloc] init];
    resource.data = data;
    resource.contentType = contentType;

    @synchronized (resources) { resources[path] = resource; }
}

- (BOOL) startOnPort: (uint16_t) port
{
    if (identity == nil)
    {
        log (@"cannot start: no TLS identity");
        return NO;
    }

    nw_parameters_t parameters = nw_parameters_create_secure_tcp (
        ^(nw_protocol_options_t tlsOptions)
        {
            sec_protocol_options_t securityOptions = nw_tls_copy_sec_protocol_options (tlsOptions);
            sec_protocol_options_set_local_identity (securityOptions, self->identity);
        },
        NW_PARAMETERS_DEFAULT_CONFIGURATION);

    // Loopback only. Nothing here should ever be reachable from the network -
    // the whole point is that the bytes never leave the device.
    nw_endpoint_t local = nw_endpoint_create_host ("127.0.0.1",
                                                   [NSString stringWithFormat: @"%u", port].UTF8String);
    nw_parameters_set_local_endpoint (parameters, local);
    nw_parameters_set_reuse_local_address (parameters, true);

    listener = nw_listener_create (parameters);

    if (listener == nil)
    {
        log (@"nw_listener_create returned nil");
        return NO;
    }

    nw_listener_set_queue (listener, queue);
    listenerReady = NO;
    readySemaphore = dispatch_semaphore_create (0);

    nw_listener_set_state_changed_handler (listener, ^(nw_listener_state_t state, nw_error_t error)
    {
        switch (state)
        {
            case nw_listener_state_ready:
                self->log ([NSString stringWithFormat: @"listener ready on port %u", nw_listener_get_port (self->listener)]);
                self->listenerReady = YES;
                dispatch_semaphore_signal (self->readySemaphore);
                break;
            case nw_listener_state_failed:
                self->log ([NSString stringWithFormat: @"listener failed: %@", error]);
                dispatch_semaphore_signal (self->readySemaphore);
                break;
            case nw_listener_state_cancelled:
                self->log (@"listener cancelled");
                dispatch_semaphore_signal (self->readySemaphore);
                break;
            default:
                break;
        }
    });

    nw_listener_set_new_connection_handler (listener, ^(nw_connection_t connection)
    {
        [self handleConnection: connection];
    });

    nw_listener_start (listener);
    return YES;
}

- (BOOL) waitUntilReadyWithTimeout: (NSTimeInterval) timeout
{
    if (listenerReady)
        return YES;

    if (readySemaphore == nil)
        return NO;

    dispatch_semaphore_wait (readySemaphore,
                             dispatch_time (DISPATCH_TIME_NOW, (int64_t) (timeout * NSEC_PER_SEC)));
    return listenerReady;
}

- (void) handleConnection: (nw_connection_t) connection
{
    nw_connection_set_queue (connection, queue);
    nw_connection_start (connection);

    // One request per connection is enough for this PoC: the installer daemon
    // opens a fresh connection for the manifest and for the package.
    nw_connection_receive (connection, 1, 8192,
        ^(dispatch_data_t content, nw_content_context_t context, bool isComplete, nw_error_t error)
        {
            if (content == NULL)
            {
                if (error != NULL)
                    self->log ([NSString stringWithFormat: @"receive error: %@", error]);

                nw_connection_cancel (connection);
                return;
            }

            NSData* requestData = (NSData*) content;
            NSString* request = [[NSString alloc] initWithData: requestData encoding: NSUTF8StringEncoding];
            NSString* path = [self pathFromRequest: request];

            self->log ([NSString stringWithFormat: @"request: %@", path ?: @"(unparsed)"]);
            [self respondOn: connection toPath: path];
        });
}

- (NSString*) pathFromRequest: (NSString*) request
{
    if (request.length == 0)
        return nil;

    NSArray<NSString*>* lines = [request componentsSeparatedByString: @"\r\n"];
    NSArray<NSString*>* parts = [lines.firstObject componentsSeparatedByString: @" "];
    return parts.count >= 2 ? parts[1] : nil;
}

- (void) respondOn: (nw_connection_t) connection toPath: (NSString*) path
{
    ServedResource* resource = nil;

    if (path != nil)
        @synchronized (resources) { resource = resources[path]; }

    NSMutableData* response = [NSMutableData data];

    if (resource != nil)
    {
        NSString* header = [NSString stringWithFormat:
            @"HTTP/1.1 200 OK\r\n"
             "Content-Type: %@\r\n"
             "Content-Length: %lu\r\n"
             "Connection: close\r\n\r\n",
            resource.contentType, (unsigned long) resource.data.length];

        [response appendData: [header dataUsingEncoding: NSUTF8StringEncoding]];
        [response appendData: resource.data];
        log ([NSString stringWithFormat: @"served %@ (%lu bytes)", path, (unsigned long) resource.data.length]);
    }
    else
    {
        NSString* notFound = @"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
        [response appendData: [notFound dataUsingEncoding: NSUTF8StringEncoding]];
        log ([NSString stringWithFormat: @"404 for %@", path ?: @"(none)"]);
    }

    dispatch_data_t payload = dispatch_data_create (response.bytes, response.length, queue,
                                                    DISPATCH_DATA_DESTRUCTOR_DEFAULT);

    nw_connection_send (connection, payload, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT, true,
        ^(nw_error_t error)
        {
            if (error != NULL)
                self->log ([NSString stringWithFormat: @"send error: %@", error]);

            nw_connection_cancel (connection);
        });
}

- (void) stop
{
    if (listener != nil)
    {
        nw_listener_cancel (listener);
        listener = nil;
    }

    if (readySemaphore != nil)
        dispatch_semaphore_signal (readySemaphore);
}

@end

#else

@implementation LoopbackServer
- (instancetype) initWithIdentityData: (NSData*) identityData
                           passphrase: (NSString*) passphrase
                               logger: (void (^)(NSString*)) logger
{
    (void) identityData;
    (void) passphrase;
    (void) logger;
    return [super init];
}

- (void) serveData: (NSData*) data atPath: (NSString*) path contentType: (NSString*) contentType
{
    (void) data;
    (void) path;
    (void) contentType;
}

- (BOOL) startOnPort: (uint16_t) port
{
    (void) port;
    return NO;
}

- (BOOL) waitUntilReadyWithTimeout: (NSTimeInterval) timeout
{
    (void) timeout;
    return NO;
}

- (void) stop {}
@end

#endif
