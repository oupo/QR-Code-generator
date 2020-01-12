#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "BitBuffer.hpp"
#include "QrCode.hpp"

using std::uint8_t;
using qrcodegen::QrCode;
using qrcodegen::QrSegment;

int main() {
	const char *text = "Hello, world!";
	const QrCode::Ecc errCorLvl = QrCode::Ecc::LOW;
	const QrCode qr = QrCode::encodeText(text, errCorLvl);
	std::cout << qr.toSvgString(4) << std::endl;
	return EXIT_SUCCESS;
}
