#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include "BitBuffer.hpp"
#include "QrCode.hpp"
#include <chrono>

using std::uint8_t;
using qrcodegen::QrCode;
using qrcodegen::QrSegment;

const int BLOCK_SIZE = 0x900;

void writeRandomData(std::vector<uint8_t>& data, uint32_t seed) {
	data.resize(BLOCK_SIZE);
	for (int i = 0; i < BLOCK_SIZE / 4; i++) {
		data[i * 4 + 0] = seed & 0xff;
		data[i * 4 + 1] = (seed >> 8) & 0xff;
		data[i * 4 + 2] = (seed >> 16) & 0xff;
		data[i * 4 + 3] = (seed >> 24) & 0xff;
		seed = seed * 0x41c64e6d + 0x6073;
	}
}

QrCode dumpQR(int blockid, uint8_t* buf, int len)
{
	std::vector<uint8_t> vec(4 + len);
	vec[0] = blockid & 0xff;
	vec[1] = (blockid >> 8) & 0xff;
	vec[2] = (blockid >> 16) & 0xff;
	vec[3] = (blockid >> 24) & 0xff;
	std::copy(buf, buf + len, vec.begin() + 4);
	std::vector<QrSegment> segs{ QrSegment::makeEci(27), QrSegment::makeBytes(vec) };
	int mask = 7;
	return QrCode::encodeSegmentsWide(segs, mask);
}

int main() {
	QrCode::initialize(30);
	std::vector<uint8_t> testdata;
	writeRandomData(testdata, 0xdeadbeef);
	auto start = std::chrono::steady_clock::now();
	for (int i = 0; i < 3000; i++) {
		QrCode qr = dumpQR(-1, &testdata[0], BLOCK_SIZE);
	}
	auto end = std::chrono::steady_clock::now();
	std::cerr << std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / 1e9 << std::endl;
	return EXIT_SUCCESS;
}
