#include <map>
#include <string>

class Sensor {
    public:
        virtual int initialize(std::map<std::string, std::string> args);
        virtual double read_data();
};