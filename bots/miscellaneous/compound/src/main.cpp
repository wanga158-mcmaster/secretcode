// contenders/compound: dispersed durable farmers that reinvest gold at once.
// The turn boundary catches every exception, logs one line containing "error" and yields.
#include <cambc.h>
#include <exception>
#include <iostream>

#include "brain.h"
#include "draft.h"

int main()
{
    cb::Brain brain;
    int errors = 0;
    while (true)
    {
        try
        {
            if (cambc::is_draft())
                play_draft();
            else
                brain.turn();
        }
        catch (const std::exception& e)
        {
            if (++errors <= 20) std::cout << "compound error r" << cambc::round_number() << ": " << e.what() << '\n';
        }
        catch (...)
        {
            if (++errors <= 20) std::cout << "compound error: unknown exception\n";
        }
        cambc::end_turn();
    }
}
