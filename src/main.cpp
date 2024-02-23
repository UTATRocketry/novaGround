#include <chrono>
#include <thread>
#include <iostream>
#include <daqhats/daqhats.h>


int main(){
	std::cout << hat_list(0, NULL);
	
	int frequency = 10; //measured in Hertz
	unsigned int loop_repeats= 100; 
	int i = 1;
	while(i<=loop_upper_bound){
		std::cout<<"Sample "<<i<<std::endl; 	
		std::this_thread::sleep_for(std::chrono::milliseconds(1000/frequency)); 	
		i++; 	
	}
}	
