#pragma once

#include <string>
#include <utility>

template <typename DataType> class Sensor {
  public:
    Sensor(int id, std::string name)
        : id(id), name(std::move(std::move(name))){};
    virtual auto query() -> DataType = 0;

    int id;
    std::string name;
};