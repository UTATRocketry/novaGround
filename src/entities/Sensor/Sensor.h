#include <map>
#include <string>

class Sensor {
    public:
        int initialize(std::map<std::string, std::string> args);
        double read_data();
};