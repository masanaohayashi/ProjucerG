#pragma once

#import <Foundation/Foundation.h>

/** A minimal HTTPS server bound to the loopback interface.

    It exists for one reason: iOS will only accept an `itms-services` manifest
    over HTTPS with a certificate it already trusts, and there is no way to make
    it trust a self-signed one without the user installing a CA profile by hand.

    The way round that is a certificate for a domain whose DNS points back at
    127.0.0.1 - *.backloop.dev publishes exactly such a certificate, key
    included. The server below presents it, so a connection to
    https://<sub>.backloop.dev:<port>/ is a genuinely trusted HTTPS connection
    that never leaves the device.
*/
@interface LoopbackServer : NSObject

/** @param identityData  PKCS#12 blob holding the certificate and its key.
    @param logger        called for every server event, on an arbitrary queue. */
- (instancetype) initWithIdentityData: (NSData*) identityData
                           passphrase: (NSString*) passphrase
                               logger: (void (^)(NSString*)) logger;

/** Serves @c data at @c path (e.g. "/manifest.plist") with @c contentType. */
- (void) serveData: (NSData*) data atPath: (NSString*) path contentType: (NSString*) contentType;

/** Returns NO and logs the reason on failure. */
- (BOOL) startOnPort: (uint16_t) port;
- (void) stop;

@end
