#pragma once

#include <iostream>
#include <string>

class ConsoleUtils
{
public:
    static void waitForUserInput()
    {
        while (true)
        {
            std::string value;
            std::cin >> value;

            if (value == "q")
            {
                break;
            }

            std::cout << std::endl;
        }
    }

private:
    ConsoleUtils() = default;
};
