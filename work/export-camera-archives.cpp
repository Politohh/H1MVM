#include "component/demo/cod4_demo_reader.hpp"
int main(int argc, char** argv) {
 if (argc != 3) return 1;
 auto track=cod4::load(argv[1]);
 std::ofstream out(argv[2],std::ios::binary);
 out.exceptions(std::ios::badbit|std::ios::failbit);
 for(const auto& f:track.frames) {
  out.write(reinterpret_cast<const char*>(&f.time),4);
  out.write(reinterpret_cast<const char*>(f.origin.data()),12);
  out.write(reinterpret_cast<const char*>(f.velocity.data()),12);
  out.write(reinterpret_cast<const char*>(&f.bob_cycle),4);
  out.write(reinterpret_cast<const char*>(&f.movement_dir),4);
  out.write(reinterpret_cast<const char*>(f.angles.data()),12);
 }
}
