#include <iostream>
#include <vector>
#include <random>
#include "dummysensor.h"

double dummysensor(double lower, double upper) {
    std::random_device rd;
    std::default_random_engine re(rd());
    std::uniform_real_distribution<double> unif(lower, upper);
    return unif(re);
}

// for testing
//int main(){
//	for (int i = 0; i<10; i++){
//        double my_num = dummysensor(3.0, 4.0);
//        std::cout<<my_num<<std::endl;
//	}
//	return 0;
//}
