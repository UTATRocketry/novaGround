#include <iostream>

bool change_state(bool current_state) {
    std::cout << "State changed";
    return not(current_state);
}