#pragma once

#include <string>
#include <vector>

/*  Split Server-Sent Event bytes into the payloads of completed events.

    This does not depend on JUCE. Pass network bytes directly to feed() and
    each payload is returned as soon as its event is complete. Delivery can be
    split or combined arbitrarily.

    Multiple data: lines in one event are joined with \n as required by SSE.
    Lines other than data: (event:, id:, and comments beginning with :) are ignored.
*/
class SseParser
{
public:
    /*  Feed received bytes and return the payloads of events completed by them. */
    std::vector<std::string> feed (const std::string& bytes);

private:
    std::string buffer;    /*  Remaining bytes that do not yet form a complete line. */
    std::string pending;   /*  Payload of the event currently being assembled. */
    bool hasPending = false;
};
