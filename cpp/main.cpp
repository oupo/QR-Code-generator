#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <cstring>
#include <vector>
#include "BitBuffer.hpp"
#include "QrCode.hpp"

using std::uint8_t;
using qrcodegen::QrCode;
using qrcodegen::QrSegment;

int main() {
	const unsigned char text[] = "Hello, world!";
	int len = strlen((const char *)text);
	std::vector<uint8_t> vec(len);
	vec.assign(text, text + len);
	
	std::vector<QrSegment> segs{QrSegment::makeBytes(vec)};
	const QrCode qr = QrCode::encodeSegmentsWide(segs);
	std::cout << qr.toSvgString(4) << std::endl;
	return EXIT_SUCCESS;
}
