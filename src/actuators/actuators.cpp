#include <iostream>

bool change_state(bool current_state) {
    bool changed_state = not(current_state);
    std::cout << "State changed to " << changed_state;
    return changed_state;
}