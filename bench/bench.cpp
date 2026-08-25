#include <array_book.hpp>
#include <iostream>

#include "types.hpp"

int main() {
  ob::PriceBand band{.floor = 9000, .ceil = 11000};  // 90.00 - 110.00
  ArrayBook book{band};
  std::cout << "Hello from my order book\n";
  return 0;
}
