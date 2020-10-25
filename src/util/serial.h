#pragma once
  
#include <iostream>
#include <vector>

namespace util {

class encoder {
  public: 
  inline encoder(std::ostream& target_stream) : out_stream(target_stream) {}

  // encodes into bytes (most significant first)
  void encode(unsigned int t);
  void encode(int t);
  void encode(char t); 
  
  private:
  std::ostream& out_stream;
};

}  // namespace util

