#include "components/micronet/protocol.h"
#include "components/micronet_compass/math.h"
#include <cassert>
#include <iostream>
#include <random>
#include <vector>
using namespace esphome::micronet;
using Bytes = std::vector<uint8_t>;
static bool near(float a, float b) { return std::fabs(a-b) < 0.0001f; }
static Bytes field(std::initializer_list<uint8_t> data) {
  Bytes f(data); f.push_back(checksum(f.data(), f.size())); return f;
}
static Bytes packet(const std::vector<Bytes> &fields, uint8_t type=2) {
  Bytes p{0x83,3,0x77,0x37,2,3,0x90,0x87,type,1,9,0,0,0};
  p[11] = checksum(p.data(), 11);
  for (auto &f : fields) p.insert(p.end(), f.begin(), f.end());
  p[12] = p[13] = p.size()-2; return p;
}
int main() {
  // Verbatim documented MicroNav wind transducer example: 4.4 kt, -5 degrees.
  Bytes wind{0x83,3,0x77,0x37,2,3,0x90,0x87,2,1,9,0x5c,0x18,0x18,
             4,5,5,0,0x2c,0x3a,4,6,5,0xff,0xfb,9};
  Decoded d;
  assert(decode(wind.data(), wind.size(), d));
  assert(d.network_id == 0x83037737 && d.device_id == 0x02039087 && d.count == 2);
  assert(d.readings[0].kind == Measurement::WIND_SPEED && near(d.readings[0].value, 4.4f * 1852/3600));
  assert(d.readings[1].kind == Measurement::WIND_ANGLE && near(d.readings[1].value, -5 * 0.01745329252f));
  // Every truncation is rejected, and failure leaves the output unchanged.
  for (size_t n = 0; n < wind.size(); ++n) assert(!decode(wind.data(), n, d));
  for (size_t i = 0; i < wind.size(); ++i) {
    Bytes corrupt = wind; corrupt[i] ^= 1;
    d.count = 19;
    assert(!decode(corrupt.data(), corrupt.size(), d)); assert(d.count == 19);
  }
  auto values = packet({field({4,1,5,0,100}), field({4,4,5,0,100}), field({3,3,5,0xfc}),
                        field({4,7,5,1,0x68}), field({6,8,5,0,100,0,90}),
                        field({10,2,5,0,0,0,100,0,0,0,10})});
  assert(decode(values.data(), values.size(), d) && d.count == 8);
  assert(near(d.readings[0].value, 1852.0f/3600));
  assert(near(d.readings[1].value, 3.048f));
  assert(near(d.readings[2].value, 271.15f));
  assert(near(d.readings[3].value, 0));
  assert(near(d.readings[4].value, 10*1852.0f/3600));
  assert(near(d.readings[5].value, 1.5707963268f));
  assert(near(d.readings[6].value, 1852)); assert(near(d.readings[7].value, 1852));
  auto unavailable = packet({field({4,4,5,0x13,0x89})});
  assert(decode(unavailable.data(), unavailable.size(), d) && d.count == 0);
  auto unknown = packet({field({3,0x77,5,0}), field({4,5,5,0,10})});
  assert(decode(unknown.data(), unknown.size(), d) && d.count == 1);
  auto short_field = packet({Bytes{0,0}});
  assert(!decode(short_field.data(), short_field.size(), d));
  auto master = packet({},1); assert(decode(master.data(), master.size(), d) && d.count == 0);
  // Bounded fuzz with valid headers forces arbitrary bytes through field parsing.
  std::mt19937 rng(78412);
  for (size_t n=14; n <= MAX_PACKET; ++n) {
    for (int trial=0; trial<500; ++trial) {
      Bytes p(n); for (auto &v : p) v = rng() & 255;
      p[8]=2; p[11]=checksum(p.data(),11); p[12]=p[13]=n-2;
      decode(p.data(),p.size(),d);
    }
  }
  using esphome::micronet_compass::heading;
  assert(near(heading(1,0,0,0,0,1,0),0));
  assert(near(heading(0,-1,0,0,0,1,0),1.5707963268f));
  assert(near(heading(-1,0,0,0,0,1,0),3.1415926536f));
  assert(near(heading(0,1,0,0,0,1,0),4.7123889804f));
  // Pitch both gravity and north 30 degrees: still points north.
  assert(near(heading(.8660254f,0,-.5f,.5f,0,.8660254f,0),0));
  assert(std::isnan(heading(0,0,0,0,0,1,0)));
  assert(std::isnan(heading(1,0,0,0,0,0,0)));
  assert(std::isnan(heading(NAN,0,0,0,0,1,0)));
  std::cout << "Decoder fixtures, corruption, truncation, 41500 fuzz cases and compass checks passed\n";
}
