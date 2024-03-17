#include <iostream>
#include <daqhats/mcc128.h>
#include <daqhats/daqhats.h>
#include <vector>
#include <cstdint>

std::vector<int> initialize_daqs(){
    // this works with minimal testing.
    
    std::vector<int> connected_daqs;
	int count = hat_list(HAT_ID_ANY, NULL);
	//std::cout<<"Number of DAQs connected: " <<count<<std::endl; 

	if (count>0){
		struct HatInfo* list = (struct HatInfo*)malloc(count*sizeof(struct HatInfo)); 
		hat_list(HAT_ID_ANY, list); 
		
		for (int i = 0; i < count; i++){
            connected_daqs.push_back(+list[i].address); 
			//useful for like debugging 
			//std::cout<< "Address " << +list[i].address <<std::endl; 
			//std::cout<< "Version" << list[i].version<<std::endl;
			//std::cout<< "Product Name" << list[i].product_name<<std::endl;
			//std::cout<< "ID" << list[i].id<<std::endl;
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
			
			std::cout<< "Address: " << +list[i].address <<std::endl; 
			std::cout<< "Version: " << list[i].version<<std::endl;
			std::cout<< "Product Name: " << list[i].product_name<<std::endl;
			std::cout<< "ID: " << list[i].id<<std::endl;
		}
		
		free(list); 
	}
    
    
    return connected_daqs;
}

int close_daqs(std::vector<int> list_of_daqs){
    // NOTE: THIS IS HARDCODED FOR MCC128s, AND WILL NOT, IN THE CURRENT CONFIGURATION, WORK OTHERWISE.
    // Currently doesn't seem to work. result_code keeps returning -1 
    for (int i = 0; i<(int)list_of_daqs.capacity(); i++){
        std::cout<<list_of_daqs[i]<<std::endl; 
        int result_code = mcc128_close((uint8_t)list_of_daqs[i]); 
        std::cout<<+result_code<<std::endl; 
        if (result_code != RESULT_SUCCESS){
            std::cout<<"ERROR IN close_daqs()"<<std::endl; 
            return 0; 
        }
    }
    return 1; 
    

}

double getDaqValue(int address, int channel) {
   
    double value;
    int result; 
    uint32_t options = OPTS_DEFAULT;
    result = mcc128_a_in_read(address, channel, options , &value);
    
    return value;
}


