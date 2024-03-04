#include <iostream>
#include <daqhats/mcc128.h>
#include <daqhats/daqhats.h>
#include <vector>

std::vector<int> initialize_daqs(){
    // Return a vector<int> with the addresses of the connected DAQs

    
    std::vector<int> connected_daqs;
	int count = hat_list(HAT_ID_ANY, NULL);
	std::cout<<"Number of DAQs connected: " <<count<<std::endl; 

	if (count>0){
		struct HatInfo* list = (struct HatInfo*)malloc(count*sizeof(struct HatInfo)); 
		hat_list(HAT_ID_ANY, list); 
		
		for (int i = 0; i < count; i++){
			
			std::cout<< "Address " << +list[i].address <<std::endl; 
			std::cout<< "Version" << list[i].version<<std::endl;
			std::cout<< "Product Name" << list[i].product_name<<std::endl;
			std::cout<< "ID" << list[i].id<<std::endl;
		}
		
		free(list); 
	}
    
    
    return connected_daqs;
}

std::vector<int> get_available_128daqs(){
    std::vector<int> connected_daqs;
	int count = hat_list(HAT_ID_ANY, NULL);
	std::cout<<"Number of DAQs connected: " <<count<<std::endl; 

	if (count>0){
		struct HatInfo* list = (struct HatInfo*)malloc(count*sizeof(struct HatInfo)); 
		hat_list(HAT_ID_ANY, list); 
		
		for (int i = 0; i < count; i++){
			
			std::cout<< "Address " << +list[i].address <<std::endl; 
			std::cout<< "Version" << list[i].version<<std::endl;
			std::cout<< "Product Name" << list[i].product_name<<std::endl;
			std::cout<< "ID" << list[i].id<<std::endl;
		}
		
		free(list); 
	}
    
    
    return connected_daqs;
}

void close_daqs(){

}

double getDaqValue(int address, int channel) {
    double value;
    int result;
    uint32_t options = OPTS_DEFAULT;
    result = mcc128_a_in_read(address, channel, options , &value);
    return value;
}


