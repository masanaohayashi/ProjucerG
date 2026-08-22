#include "jucer_Keychain.h"

#import <Foundation/Foundation.h>
#import <Security/Security.h>

/*  このプロジェクトは ARC を使わない。Projucer の Xcode exporter は
    CLANG_ENABLE_OBJC_ARC を一切出力せず、Xcode の既定値は NO であるため。
    したがって __bridge 系のキャストは書かない（非 ARC では警告付きの
    ノーオペになり、所有権が誰にも渡らないまま漏れる）。
    CoreFoundation と Objective-C の相互キャストは素のキャストで書き、
    Copy/Create で得た参照は明示的に CFRelease する。
*/

namespace
{
    NSMutableDictionary* baseQuery (const juce::String& service, const juce::String& account)
    {
        auto* query = [NSMutableDictionary dictionary];
        query[(id) kSecClass]       = (id) kSecClassGenericPassword;
        query[(id) kSecAttrService] = [NSString stringWithUTF8String: service.toRawUTF8()];
        query[(id) kSecAttrAccount] = [NSString stringWithUTF8String: account.toRawUTF8()];
        return query;
    }
}

bool keychainWrite (const juce::String& service, const juce::String& account, const juce::String& value)
{
    @autoreleasepool
    {
        auto* query = baseQuery (service, account);

        auto* data = [[NSString stringWithUTF8String: value.toRawUTF8()]
                          dataUsingEncoding: NSUTF8StringEncoding];

        // 既存があれば更新し、無ければ追加する。削除してから足す方式と違い、
        // 項目が一瞬消える隙間を作らない。
        auto* attributesToUpdate = [NSMutableDictionary dictionary];
        attributesToUpdate[(id) kSecValueData] = data;
        attributesToUpdate[(id) kSecAttrAccessible] = (id) kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;

        const auto updateStatus = SecItemUpdate ((CFDictionaryRef) query,
                                                 (CFDictionaryRef) attributesToUpdate);

        if (updateStatus == errSecSuccess)
            return true;

        if (updateStatus != errSecItemNotFound)
            return false;

        query[(id) kSecValueData] = data;
        query[(id) kSecAttrAccessible] = (id) kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly;

        return SecItemAdd ((CFDictionaryRef) query, nullptr) == errSecSuccess;
    }
}

juce::String keychainRead (const juce::String& service, const juce::String& account)
{
    @autoreleasepool
    {
        auto* query = baseQuery (service, account);
        query[(id) kSecReturnData] = @YES;
        query[(id) kSecMatchLimit] = (id) kSecMatchLimitOne;

        CFTypeRef result = nullptr;

        if (SecItemCopyMatching ((CFDictionaryRef) query, &result) != errSecSuccess)
            return {};

        if (result == nullptr)
            return {};

        // SecItemCopyMatching は +1 の参照を返す。ARC が無いのでここで責任を持つ。
        // NSString を挟まずバイト列から直接組み立て、解放対象を 1 つに保つ。
        auto* data = (NSData*) result;

        juce::String value;

        if ([data length] > 0)
            value = juce::String::fromUTF8 ((const char*) [data bytes], (int) [data length]);

        CFRelease (result);
        return value;
    }
}

void keychainErase (const juce::String& service, const juce::String& account)
{
    @autoreleasepool
    {
        SecItemDelete ((CFDictionaryRef) baseQuery (service, account));
    }
}
