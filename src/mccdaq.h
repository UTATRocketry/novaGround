#include <vector>
double getDaqValue(int channel, int daqNum);
std::vector<int> initialize_daqs(); 
int close_daqs(std::vector<int> list_of_daqs); 
