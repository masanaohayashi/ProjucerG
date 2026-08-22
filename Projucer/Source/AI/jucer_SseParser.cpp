#include "jucer_SseParser.h"

std::vector<std::string> SseParser::feed (const std::string& bytes)
{
    buffer += bytes;

    std::vector<std::string> events;
    std::size_t pos = 0;

    for (;;)
    {
        const auto newline = buffer.find ('\n', pos);

        if (newline == std::string::npos)
            break;

        auto line = buffer.substr (pos, newline - pos);
        pos = newline + 1;

        if (! line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty())
        {
            /*  A blank line separates events. */
            if (hasPending)
            {
                events.push_back (pending);
                pending.clear();
                hasPending = false;
            }

            continue;
        }

        if (line.rfind ("data:", 0) != 0)
            continue;   /*  Ignore event:, id:, and comments. */

        auto payload = line.substr (5);

        if (! payload.empty() && payload.front() == ' ')
            payload.erase (0, 1);

        if (hasPending)
            pending += '\n';

        pending += payload;
        hasPending = true;
    }

    buffer.erase (0, pos);
    return events;
}
