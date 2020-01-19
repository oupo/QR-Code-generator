/* 
 * QR Code generator library (C++)
 * 
 * Copyright (c) Project Nayuki. (MIT License)
 * https://www.nayuki.io/page/qr-code-generator-library
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * - The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 * - The Software is provided "as is", without warranty of any kind, express or
 *   implied, including but not limited to the warranties of merchantability,
 *   fitness for a particular purpose and noninfringement. In no event shall the
 *   authors or copyright holders be liable for any claim, damages or other
 *   liability, whether in an action of contract, tort or otherwise, arising from,
 *   out of or in connection with the Software or the use or other dealings in the
 *   Software.
 */

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <sstream>
#include <utility>
#include "BitBuffer.hpp"
#include "QrCode.hpp"

using std::int8_t;
using std::uint8_t;
using std::size_t;
using std::vector;


namespace qrcodegen {

static uint8_t REED_SOLOMON_MULTIPLY[256][256];

static uint8_t MASK[8][6][6];

void QrCode::initialize() {
	for (int x = 0; x < 256; x++) {
		for (int y = 0; y < 256; y++) {
			REED_SOLOMON_MULTIPLY[x][y] = reedSolomonMultiply(x, y);
		}
	}
	for (int y = 0; y < 6; y++) {
		for (int x = 0; x < 6; x++) {
			MASK[0][y][x] = (x + y) % 2 == 0;
			MASK[1][y][x] = y % 2 == 0;
			MASK[2][y][x] = x % 3 == 0;
			MASK[3][y][x] = (x + y) % 3 == 0;
			MASK[4][y][x] = (x / 3 + y / 2) % 2 == 0;
			MASK[5][y][x] = x * y % 2 + x * y % 3 == 0;
			MASK[6][y][x] = (x * y % 2 + x * y % 3) % 2 == 0;
			MASK[7][y][x] = ((x + y) % 2 + x * y % 3) % 2 == 0;
		}
	}
}

int QrCode::getFormatBits(Ecc ecl) {
	switch (ecl) {
		case Ecc::LOW     :  return 1;
		case Ecc::MEDIUM  :  return 0;
		case Ecc::QUARTILE:  return 3;
		case Ecc::HIGH    :  return 2;
		default:  throw std::logic_error("Assertion error");
	}
}


QrCode QrCode::encodeText(const char *text, Ecc ecl) {
	vector<QrSegment> segs = QrSegment::makeSegments(text);
	return encodeSegments(segs, ecl);
}


QrCode QrCode::encodeBinary(const vector<uint8_t> &data, Ecc ecl) {
	vector<QrSegment> segs{QrSegment::makeBytes(data)};
	return encodeSegments(segs, ecl);
}


QrCode QrCode::encodeSegments(const vector<QrSegment> &segs, Ecc ecl,
		int minVersion, int maxVersion, int mask, bool boostEcl) {
	if (!(MIN_VERSION <= minVersion && minVersion <= maxVersion && maxVersion <= MAX_VERSION) || mask < -1 || mask > 7)
		throw std::invalid_argument("Invalid value");
	
	// Find the minimal version number to use
	int version, dataUsedBits;
	for (version = minVersion; ; version++) {
		int dataCapacityBits = getNumDataCodewords({false, version}, ecl) * 8;  // Number of data bits available
		dataUsedBits = QrSegment::getTotalBits(segs, version);
		if (dataUsedBits != -1 && dataUsedBits <= dataCapacityBits)
			break;  // This version number is found to be suitable
		if (version >= maxVersion) {  // All versions in the range could not fit the given data
			std::ostringstream sb;
			if (dataUsedBits == -1)
				sb << "Segment too long";
			else {
				sb << "Data length = " << dataUsedBits << " bits, ";
				sb << "Max capacity = " << dataCapacityBits << " bits";
			}
			throw data_too_long(sb.str());
		}
	}
	if (dataUsedBits == -1)
		throw std::logic_error("Assertion error");
	
	// Increase the error correction level while the data still fits in the current version number
	for (Ecc newEcl : vector<Ecc>{Ecc::MEDIUM, Ecc::QUARTILE, Ecc::HIGH}) {  // From low to high
		if (boostEcl && dataUsedBits <= getNumDataCodewords({false, version}, newEcl) * 8)
			ecl = newEcl;
	}
	
	// Concatenate all segments to create the data bit string
	BitBuffer bb(dataUsedBits);
	for (const QrSegment &seg : segs) {
		bb.appendBits(static_cast<uint32_t>(seg.getMode().getModeBits()), 4);
		bb.appendBits(static_cast<uint32_t>(seg.getNumChars()), seg.getMode().numCharCountBits(version));
		bb.insert(bb.end(), seg.getData().begin(), seg.getData().end());
	}
	if (bb.size() != static_cast<unsigned int>(dataUsedBits))
		throw std::logic_error("Assertion error");
	
	// Add terminator and pad up to a byte if applicable
	size_t dataCapacityBits = static_cast<size_t>(getNumDataCodewords({false, version}, ecl)) * 8;
	if (bb.size() > dataCapacityBits)
		throw std::logic_error("Assertion error");
	bb.appendBits(0, std::min(4, static_cast<int>(dataCapacityBits - bb.size())));
	bb.appendBits(0, (8 - static_cast<int>(bb.size() % 8)) % 8);
	if (bb.size() % 8 != 0)
		throw std::logic_error("Assertion error");
	
	// Pad with alternating bytes until data capacity is reached
	for (uint8_t padByte = 0xEC; bb.size() < dataCapacityBits; padByte ^= 0xEC ^ 0x11)
		bb.appendBits(padByte, 8);
	
	// Pack bits into bytes in big endian
	vector<uint8_t> dataCodewords(bb.size() / 8);
	for (size_t i = 0; i < bb.size(); i++)
		dataCodewords[i >> 3] |= (bb.at(i) ? 1 : 0) << (7 - (i & 7));
	
	// Create the QR Code object
	return QrCode({false, version}, ecl, dataCodewords, mask);
}


QrCode QrCode::encodeSegmentsWide(const vector<QrSegment> &segs, int mask) {
	if (mask < -1 || mask > 7)
		throw std::invalid_argument("Invalid value");
	
	// Find the minimal version number to use
	Version version = {true, 0};
	Ecc dummyEcl = Ecc::MEDIUM;
	int dummyVer = 40;
	int dataCapacityBits = getNumDataCodewords(version, dummyEcl) * 8;
	int dataUsedBits = QrSegment::getTotalBits(segs, dummyVer);
	if (dataUsedBits != -1 && dataUsedBits <= dataCapacityBits) {
		// ok
	} else {
		std::ostringstream sb;
		if (dataUsedBits == -1)
			sb << "Segment too long";
		else {
			sb << "Data length = " << dataUsedBits << " bits, ";
			sb << "Max capacity = " << dataCapacityBits << " bits";
		}
		throw data_too_long(sb.str());
	}
	
	// Concatenate all segments to create the data bit string
	BitBuffer bb(dataUsedBits);
	for (const QrSegment &seg : segs) {
		bb.appendBits(static_cast<uint32_t>(seg.getMode().getModeBits()), 4);
		bb.appendBits(static_cast<uint32_t>(seg.getNumChars()), seg.getMode().numCharCountBits(dummyVer));
		bb.insert(bb.end(), seg.getData().begin(), seg.getData().end());
	}
	if (bb.size() != static_cast<unsigned int>(dataUsedBits))
		throw std::logic_error("Assertion error");
	
	// Add terminator and pad up to a byte if applicable
	if (bb.size() > dataCapacityBits)
		throw std::logic_error("Assertion error");
	bb.appendBits(0, std::min(4, static_cast<int>(dataCapacityBits - bb.size())));
	bb.appendBits(0, (8 - static_cast<int>(bb.size() % 8)) % 8);
	if (bb.size() % 8 != 0)
		throw std::logic_error("Assertion error");
	
	// Pad with alternating bytes until data capacity is reached
	for (uint8_t padByte = 0xEC; bb.size() < dataCapacityBits; padByte ^= 0xEC ^ 0x11)
		bb.appendBits(padByte, 8);
	
	// Pack bits into bytes in big endian
	vector<uint8_t> dataCodewords(bb.size() / 8);
	for (size_t i = 0; i < bb.size(); i++)
		dataCodewords[i >> 3] |= (bb.at(i) ? 1 : 0) << (7 - (i & 7));
	
	// Create the QR Code object
	return QrCode(version, Ecc::MEDIUM, dataCodewords, mask);
}


QrCode::QrCode(Version ver, Ecc ecl, const vector<uint8_t> &dataCodewords, int msk) :
		// Initialize fields and check arguments
		version(ver),
		errorCorrectionLevel(ecl) {
	if (!ver.wide && (ver.version < MIN_VERSION || ver.version > MAX_VERSION))
		throw std::domain_error("Version value out of range");
	if (msk < -1 || msk > 7)
		throw std::domain_error("Mask value out of range");
	if (ver.wide) {
		size = 0;
	} else {
		size = ver.version * 4 + 17;
	}
	size_t sz = static_cast<size_t>(size);
	modules    = vector<vector<bool> >(getHeight(), vector<bool>(getWidth()));  // Initially all white
	isFunction = vector<vector<bool> >(getHeight(), vector<bool>(getWidth()));
	
	// Compute ECC, draw modules
	drawFunctionPatterns();
	const vector<uint8_t> allCodewords = addEccAndInterleave(dataCodewords);
	if (version.wide) {
		drawCodewordsWide(allCodewords, msk);
	} else {
		drawCodewords(allCodewords);
		// Do masking
		if (msk == -1) {  // Automatically choose best mask
			long minPenalty = LONG_MAX;
			for (int i = 0; i < 8; i++) {
				applyMask(i);
				drawFormatBits(i);
				long penalty = getPenaltyScore();
				if (penalty < minPenalty) {
					msk = i;
					minPenalty = penalty;
				}
				applyMask(i);  // Undoes the mask due to XOR
			}
		}
		if (msk < 0 || msk > 7)
			throw std::logic_error("Assertion error");
		this->mask = msk;
		applyMask(msk);  // Apply the final choice of mask
		drawFormatBits(msk);  // Overwrite old format bits
	}
	isFunction.clear();
	isFunction.shrink_to_fit();
}


int QrCode::getVersion() const {
	return version.version;
}


int QrCode::getSize() const {
	if (version.wide) {
		throw "can't call getSize() in wide ver";
	}
	return size;
}


int QrCode::getWidth() const {
	return version.wide ? WIDE_WIDTH : size;
}


int QrCode::getHeight() const {
	return version.wide ? WIDE_HEIGHT : size;
}


QrCode::Ecc QrCode::getErrorCorrectionLevel() const {
	return errorCorrectionLevel;
}


int QrCode::getMask() const {
	return mask;
}


bool QrCode::getModule(int x, int y) const {
	return 0 <= x && x < getWidth() && 0 <= y && y < getHeight() && module(x, y);
}


std::string QrCode::toSvgString(int border) const {
	if (border < 0)
		throw std::domain_error("Border must be non-negative");
	if (border > INT_MAX / 2 || border * 2 > INT_MAX - size)
		throw std::overflow_error("Border too large");
	
	std::ostringstream sb;
	sb << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
	sb << "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n";
	sb << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" viewBox=\"0 0 ";
	sb << (getWidth() + border * 2) << " " << (getHeight() + border * 2) << "\" stroke=\"none\">\n";
	sb << "\t<rect width=\"100%\" height=\"100%\" fill=\"#FFFFFF\"/>\n";
	sb << "\t<path d=\"";
	for (int y = 0; y < getHeight(); y++) {
		for (int x = 0; x < getWidth(); x++) {
			if (getModule(x, y)) {
				if (x != 0 || y != 0)
					sb << " ";
				sb << "M" << (x + border) << "," << (y + border) << "h1v1h-1z";
			}
		}
	}
	sb << "\" fill=\"#000000\"/>\n";
	sb << "</svg>\n";
	return sb.str();
}


void QrCode::drawFunctionPatterns() {
	if (version.wide) {
		return;

	}
	// Draw horizontal and vertical timing patterns
	for (int i = 0; i < getHeight(); i++) {
		setFunctionModule(6, i, i % 2 == 0);
	}
	for (int i = 0; i < getWidth(); i++) {
		setFunctionModule(i, 6, i % 2 == 0);
	}
	
	// Draw 3 finder patterns (all corners except bottom right; overwrites some timing modules)
	drawFinderPattern(3, 3);
	drawFinderPattern(getWidth() - 4, 3);
	drawFinderPattern(3, getHeight() - 4);
	
	// Draw numerous alignment patterns
	const vector<int> alignPatPosX = getAlignmentPatternPositionsX();
	const vector<int> alignPatPosY = getAlignmentPatternPositionsY();
	size_t numAlignX = alignPatPosX.size();
	size_t numAlignY = alignPatPosY.size();
	for (size_t i = 0; i < numAlignX; i++) {
		for (size_t j = 0; j < numAlignY; j++) {
			// Don't draw on the three finder corners
			if (!((i == 0 && j == 0) || (i == 0 && j == numAlignY - 1) || (i == numAlignX - 1 && j == 0)))
				drawAlignmentPattern(alignPatPosX.at(i), alignPatPosY.at(j));
		}
	}
	
	// Draw configuration data
	drawFormatBits(0);  // Dummy mask value; overwritten later in the constructor
	drawVersion();
}


void QrCode::drawFormatBits(int msk) {
	// Calculate error correction code and pack bits
	int data = getFormatBits(errorCorrectionLevel) << 3 | msk;  // errCorrLvl is uint2, msk is uint3
	int rem = data;
	for (int i = 0; i < 10; i++)
		rem = (rem << 1) ^ ((rem >> 9) * 0x537);
	int bits = (data << 10 | rem) ^ 0x5412;  // uint15
	if (bits >> 15 != 0)
		throw std::logic_error("Assertion error");
	
	// Draw first copy
	for (int i = 0; i <= 5; i++)
		setFunctionModule(8, i, getBit(bits, i));
	setFunctionModule(8, 7, getBit(bits, 6));
	setFunctionModule(8, 8, getBit(bits, 7));
	setFunctionModule(7, 8, getBit(bits, 8));
	for (int i = 9; i < 15; i++)
		setFunctionModule(14 - i, 8, getBit(bits, i));
	
	// Draw second copy
	for (int i = 0; i < 8; i++)
		setFunctionModule(getWidth() - 1 - i, 8, getBit(bits, i));
	for (int i = 8; i < 15; i++)
		setFunctionModule(8, getHeight() - 15 + i, getBit(bits, i));
	setFunctionModule(8, getHeight() - 8, true);  // Always black
}


void QrCode::drawVersion() {
	int ver = version.wide ? 41 : version.version;
	if (ver < 7)
		return;
	
	// Calculate error correction code and pack bits
	int rem = ver;  // version is uint6, in the range [7, 40]
	for (int i = 0; i < 12; i++)
		rem = (rem << 1) ^ ((rem >> 11) * 0x1F25);
	long bits = static_cast<long>(ver) << 12 | rem;  // uint18
	if (bits >> 18 != 0)
		throw std::logic_error("Assertion error");
	
	// Draw two copies
	for (int i = 0; i < 18; i++) {
		bool bit = getBit(bits, i);
		int ax = getWidth() - 11 + i % 3;
		int ay = getHeight() - 11 + i % 3;
		int b = i / 3;
		setFunctionModule(ax, b, bit);
		setFunctionModule(b, ay, bit);
	}
}


void QrCode::drawFinderPattern(int x, int y) {
	for (int dy = -4; dy <= 4; dy++) {
		for (int dx = -4; dx <= 4; dx++) {
			int dist = std::max(std::abs(dx), std::abs(dy));  // Chebyshev/infinity norm
			int xx = x + dx, yy = y + dy;
			if (0 <= xx && xx < getWidth() && 0 <= yy && yy < getHeight())
				setFunctionModule(xx, yy, dist != 2 && dist != 4);
		}
	}
}


void QrCode::drawAlignmentPattern(int x, int y) {
	for (int dy = -2; dy <= 2; dy++) {
		for (int dx = -2; dx <= 2; dx++)
			setFunctionModule(x + dx, y + dy, std::max(std::abs(dx), std::abs(dy)) != 1);
	}
}


void QrCode::setFunctionModule(int x, int y, bool isBlack) {
	size_t ux = static_cast<size_t>(x);
	size_t uy = static_cast<size_t>(y);
	modules   .at(uy).at(ux) = isBlack;
	isFunction.at(uy).at(ux) = true;
}


bool QrCode::module(int x, int y) const {
	return modules.at(static_cast<size_t>(y)).at(static_cast<size_t>(x));
}


vector<uint8_t> QrCode::addEccAndInterleave(const vector<uint8_t> &data) const {
	if (data.size() != static_cast<unsigned int>(getNumDataCodewords(version, errorCorrectionLevel)))
		throw std::invalid_argument("Invalid argument");
	
	// Calculate parameter numbers
	int numBlocks = NumErrorCorrectionBlocks(errorCorrectionLevel, version);
	int blockEccLen = ECCCodeworksPerBlock(errorCorrectionLevel, version);
	int rawCodewords = getNumRawDataModules(version) / 8;
	int numShortBlocks = numBlocks - rawCodewords % numBlocks;
	int shortBlockLen = rawCodewords / numBlocks;
	
	// Split data into blocks and append ECC to each block
	vector<vector<uint8_t> > blocks;
	const vector<uint8_t> rsDiv = reedSolomonComputeDivisor(blockEccLen);
	for (int i = 0, k = 0; i < numBlocks; i++) {
		vector<uint8_t> dat(data.cbegin() + k, data.cbegin() + (k + shortBlockLen - blockEccLen + (i < numShortBlocks ? 0 : 1)));
		k += static_cast<int>(dat.size());
		const vector<uint8_t> ecc = reedSolomonComputeRemainder(dat, rsDiv);
		if (i < numShortBlocks)
			dat.push_back(0);
		dat.insert(dat.end(), ecc.cbegin(), ecc.cend());
		blocks.push_back(std::move(dat));
	}
	
	// Interleave (not concatenate) the bytes from every block into a single sequence
	vector<uint8_t> result;
	for (size_t i = 0; i < blocks.at(0).size(); i++) {
		for (size_t j = 0; j < blocks.size(); j++) {
			// Skip the padding byte in short blocks
			if (i != static_cast<unsigned int>(shortBlockLen - blockEccLen) || j >= static_cast<unsigned int>(numShortBlocks))
				result.push_back(blocks.at(j).at(i));
		}
	}
	if (result.size() != static_cast<unsigned int>(rawCodewords))
		throw std::logic_error("Assertion error");
	return result;
}


void QrCode::drawCodewords(const vector<uint8_t> &data) {
	if (data.size() != static_cast<unsigned int>(getNumRawDataModules(version) / 8))
		throw std::invalid_argument("Invalid argument");
	
	size_t i = 0;  // Bit index into the data
	// Do the funny zigzag scan
	for (int right = getWidth() - 1; right >= 1; right -= 2) {  // Index of right column in each column pair
		if (!version.wide && right == 6)
			right = 5;
		for (int vert = 0; vert < getHeight(); vert++) {  // Vertical counter
			for (int j = 0; j < 2; j++) {
				size_t x = static_cast<size_t>(right - j);  // Actual x coordinate
				bool upward = ((right + 1) & 2) == 0;
				size_t y = static_cast<size_t>(upward ? getHeight() - 1 - vert : vert);  // Actual y coordinate
				if (!isFunction.at(y).at(x) && i < data.size() * 8) {
					modules.at(y).at(x) = getBit(data.at(i >> 3), 7 - static_cast<int>(i & 7));
					i++;
				}
				// If this QR Code has any remainder bits (0 to 7), they were assigned as
				// 0/false/white by the constructor and are left unchanged by this method
			}
		}
	}
	if (i != data.size() * 8)
		throw std::logic_error("Assertion error");
}


void QrCode::drawCodewordsWide(const vector<uint8_t>& data, int msk) {
	if (data.size() != static_cast<unsigned int>(getNumRawDataModules(version) / 8))
		throw std::invalid_argument("Invalid argument");

	int w = getWidth(), h = getHeight();
	int ww = (w / 6) * 6, wmod6 = w % 6;
	size_t idx = 0;
	for (size_t y = 0; y < h; y++) {
		int j = y % 6;
		size_t x = 0;
		while (x < ww) {
			for (int i = 0; i < 6; i++) {
				modules[y][x] = getBit(data[idx >> 3], 7 - static_cast<int>(idx & 7)) ^ MASK[msk][j][i];
				x++;
				idx++;
			}
		}
		for (int i = 0; i < wmod6; i++) {
			modules[y][x] = getBit(data[idx >> 3], 7 - static_cast<int>(idx & 7)) ^ MASK[msk][j][i];
			x++;
			idx++;
		}
	}
	if (idx != data.size() * 8)
		throw std::logic_error("Assertion error");
}


void QrCode::applyMask(int msk) {
	if (msk < 0 || msk > 7)
		throw std::domain_error("Mask value out of range");
	size_t w = static_cast<size_t>(getWidth());
	size_t h = static_cast<size_t>(getHeight());
	int ww = (w / 6) * 6, wmod6 = w % 6;
	if (version.wide) {
		for (size_t y = 0; y < h; y++) {
			int j = y % 6;
			size_t x = 0;
			while (x < ww) {
				for (int i = 0; i < 6; i++) {
					modules[y][x] = modules[y][x] ^ MASK[msk][j][i];
					x++;
				}
			}
			for (int i = 0; i < wmod6; i++) {
				modules[y][x] = modules[y][x] ^ MASK[msk][j][i];
				x++;
			}
		}
	}
	else {
		for (size_t y = 0; y < h; y++) {
			for (size_t x = 0; x < w; x++) {
				bool invert = MASK[msk][y % 6][x % 6];
				modules[y][x] = modules[y][x] ^ (invert & !isFunction[y][x]);
			}
		}
	}
}


long QrCode::getPenaltyScore() const {
	if (version.wide) {
		throw "unimplemented";
	}
	long result = 0;
	
	// Adjacent modules in row having same color, and finder-like patterns
	for (int y = 0; y < size; y++) {
		bool runColor = false;
		int runX = 0;
		std::array<int,7> runHistory = {};
		int padRun = size;  // Add white border to initial run
		for (int x = 0; x < size; x++) {
			if (module(x, y) == runColor) {
				runX++;
				if (runX == 5)
					result += PENALTY_N1;
				else if (runX > 5)
					result++;
			} else {
				finderPenaltyAddHistory(runX + padRun, runHistory);
				padRun = 0;
				if (!runColor)
					result += finderPenaltyCountPatterns(runHistory) * PENALTY_N3;
				runColor = module(x, y);
				runX = 1;
			}
		}
		result += finderPenaltyTerminateAndCount(runColor, runX + padRun, runHistory) * PENALTY_N3;
	}
	// Adjacent modules in column having same color, and finder-like patterns
	for (int x = 0; x < size; x++) {
		bool runColor = false;
		int runY = 0;
		std::array<int,7> runHistory = {};
		int padRun = size;  // Add white border to initial run
		for (int y = 0; y < size; y++) {
			if (module(x, y) == runColor) {
				runY++;
				if (runY == 5)
					result += PENALTY_N1;
				else if (runY > 5)
					result++;
			} else {
				finderPenaltyAddHistory(runY + padRun, runHistory);
				padRun = 0;
				if (!runColor)
					result += finderPenaltyCountPatterns(runHistory) * PENALTY_N3;
				runColor = module(x, y);
				runY = 1;
			}
		}
		result += finderPenaltyTerminateAndCount(runColor, runY + padRun, runHistory) * PENALTY_N3;
	}
	
	// 2*2 blocks of modules having same color
	for (int y = 0; y < size - 1; y++) {
		for (int x = 0; x < size - 1; x++) {
			bool  color = module(x, y);
			if (  color == module(x + 1, y) &&
			      color == module(x, y + 1) &&
			      color == module(x + 1, y + 1))
				result += PENALTY_N2;
		}
	}
	
	// Balance of black and white modules
	int black = 0;
	for (const vector<bool> &row : modules) {
		for (bool color : row) {
			if (color)
				black++;
		}
	}
	int total = size * size;  // Note that size is odd, so black/total != 1/2
	// Compute the smallest integer k >= 0 such that (45-5k)% <= black/total <= (55+5k)%
	int k = static_cast<int>((std::abs(black * 20L - total * 10L) + total - 1) / total) - 1;
	result += k * PENALTY_N4;
	return result;
}


vector<int> QrCode::getAlignmentPatternPositionsX() const {
	return getAlignmentPatternPositionsOrig();
}


vector<int> QrCode::getAlignmentPatternPositionsY() const {
	return getAlignmentPatternPositionsOrig();
}


vector<int> QrCode::getAlignmentPatternPositionsOrig() const {
	if (version.version == 1)
		return vector<int>();
	else {
		int numAlign = version.version / 7 + 2;
		int step = (version.version == 32) ? 26 :
			(version.version*4 + numAlign*2 + 1) / (numAlign*2 - 2) * 2;
		vector<int> result;
		for (int i = 0, pos = size - 7; i < numAlign - 1; i++, pos -= step)
			result.insert(result.begin(), pos);
		result.insert(result.begin(), 6);
		return result;
	}
}


int QrCode::getNumRawDataModules(Version ver) {
	if (ver.wide) {
		return WIDE_WIDTH * WIDE_HEIGHT;
	} else {
		if (ver.version < MIN_VERSION || ver.version > MAX_VERSION)
			throw std::domain_error("Version number out of range");
		int result = (16 * ver.version + 128) * ver.version + 64;
		if (ver.version >= 2) {
			int numAlign = ver.version / 7 + 2;
			result -= (25 * numAlign - 10) * numAlign - 55;
			if (ver.version >= 7)
				result -= 36;
		}
		if (!(208 <= result && result <= 29648))
			throw std::logic_error("Assertion error");
		return result;
	}
}


int QrCode::getNumDataCodewords(Version ver, Ecc ecl) {
	return getNumRawDataModules(ver) / 8
		- ECCCodeworksPerBlock(ecl, ver)
		* NumErrorCorrectionBlocks(ecl, ver);
}


vector<uint8_t> QrCode::reedSolomonComputeDivisor(int degree) {
	if (degree < 1 || degree > 255)
		throw std::domain_error("Degree out of range");
	// Polynomial coefficients are stored from highest to lowest power, excluding the leading term which is always 1.
	// For example the polynomial x^3 + 255x^2 + 8x + 93 is stored as the uint8 array {255, 8, 93}.
	vector<uint8_t> result(static_cast<size_t>(degree));
	result.at(result.size() - 1) = 1;  // Start off with the monomial x^0
	
	// Compute the product polynomial (x - r^0) * (x - r^1) * (x - r^2) * ... * (x - r^{degree-1}),
	// and drop the highest monomial term which is always 1x^degree.
	// Note that r = 0x02, which is a generator element of this field GF(2^8/0x11D).
	uint8_t root = 1;
	for (int i = 0; i < degree; i++) {
		// Multiply the current product by (x - r^i)
		for (size_t j = 0; j < result.size(); j++) {
			result.at(j) = reedSolomonMultiply(result.at(j), root);
			if (j + 1 < result.size())
				result.at(j) ^= result.at(j + 1);
		}
		root = reedSolomonMultiply(root, 0x02);
	}
	return result;
}



vector<uint8_t> QrCode::reedSolomonComputeRemainder(const vector<uint8_t>& data, const vector<uint8_t>& divisor) {
	int len = divisor.size();
	vector<uint8_t> result(len);
	int index = 0;
	for (uint8_t b : data) {
		uint8_t factor = b ^ result[index];
		result[index] = 0;
		index++;
		if (index == len) index = 0;
		int l = len - index;
		size_t i, j;
		for (i = 0, j = index; i < l; i++, j ++)
			result[j] ^= REED_SOLOMON_MULTIPLY[factor][divisor[i]];
		j = 0;
		for (; i < len; i++, j++) {
			result[j] ^= REED_SOLOMON_MULTIPLY[factor][divisor[i]];
		}
	}
	std::rotate(result.begin(), result.begin() + index, result.end());
	return result;
}



uint8_t QrCode::reedSolomonMultiply(uint8_t x, uint8_t y) {
	// Russian peasant multiplication
	int z = 0;
	for (int i = 7; i >= 0; i--) {
		z = (z << 1) ^ ((z >> 7) * 0x11D);
		z ^= ((y >> i) & 1) * x;
	}
	if (z >> 8 != 0)
		throw std::logic_error("Assertion error");
	return static_cast<uint8_t>(z);
}

int QrCode::finderPenaltyCountPatterns(const std::array<int,7> &runHistory) const {
	int n = runHistory.at(1);
	if (n > size * 3)
		throw std::logic_error("Assertion error");
	bool core = n > 0 && runHistory.at(2) == n && runHistory.at(3) == n * 3 && runHistory.at(4) == n && runHistory.at(5) == n;
	return (core && runHistory.at(0) >= n * 4 && runHistory.at(6) >= n ? 1 : 0)
	     + (core && runHistory.at(6) >= n * 4 && runHistory.at(0) >= n ? 1 : 0);
}


int QrCode::finderPenaltyTerminateAndCount(bool currentRunColor, int currentRunLength, std::array<int,7> &runHistory) const {
	if (currentRunColor) {  // Terminate black run
		finderPenaltyAddHistory(currentRunLength, runHistory);
		currentRunLength = 0;
	}
	currentRunLength += size;  // Add white border to final run
	finderPenaltyAddHistory(currentRunLength, runHistory);
	return finderPenaltyCountPatterns(runHistory);
}


void QrCode::finderPenaltyAddHistory(int currentRunLength, std::array<int,7> &runHistory) {
	std::copy_backward(runHistory.cbegin(), runHistory.cend() - 1, runHistory.end());
	runHistory.at(0) = currentRunLength;
}


bool QrCode::getBit(long x, int i) {
	return ((x >> i) & 1) != 0;
}


int QrCode::ECCCodeworksPerBlock(Ecc ecc, Version ver) {
	if (ver.wide) {
		return WIDE_ECC_CODEWORKS_PER_BLOCK;
	} else {
		return ECC_CODEWORDS_PER_BLOCK[static_cast<int>(ecc)][ver.version];
	}
}

int QrCode::NumErrorCorrectionBlocks(Ecc ecc, Version ver) {
	if (ver.wide) {
		return WIDE_NUM_ERROR_CORRECTION_BLOCKS;
	} else {
		return NUM_ERROR_CORRECTION_BLOCKS[static_cast<int>(ecc)][ver.version];
	}
}


/*---- Tables of constants ----*/

const int QrCode::PENALTY_N1 =  3;
const int QrCode::PENALTY_N2 =  3;
const int QrCode::PENALTY_N3 = 40;
const int QrCode::PENALTY_N4 = 10;


const int8_t QrCode::ECC_CODEWORDS_PER_BLOCK[4][41] = {
	// Version: (note that index 0 is for padding, and is set to an illegal value)
	//0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40    Error correction level
	{-1,  7, 10, 15, 20, 26, 18, 20, 24, 30, 18, 20, 24, 26, 30, 22, 24, 28, 30, 28, 28, 28, 28, 30, 30, 26, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // Low
	{-1, 10, 16, 26, 18, 24, 16, 18, 22, 22, 26, 30, 22, 22, 24, 24, 28, 28, 26, 26, 26, 26, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28, 28},  // Medium
	{-1, 13, 22, 18, 26, 18, 24, 18, 22, 20, 24, 28, 26, 24, 20, 30, 24, 28, 28, 26, 30, 28, 30, 30, 30, 30, 28, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // Quartile
	{-1, 17, 28, 22, 16, 22, 28, 26, 26, 24, 28, 24, 28, 22, 24, 24, 30, 28, 28, 26, 28, 30, 24, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30, 30},  // High
};

const int8_t QrCode::NUM_ERROR_CORRECTION_BLOCKS[4][41] = {
	// Version: (note that index 0 is for padding, and is set to an illegal value)
	//0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40    Error correction level
	{-1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 4,  4,  4,  4,  4,  6,  6,  6,  6,  7,  8,  8,  9,  9, 10, 12, 12, 12, 13, 14, 15, 16, 17, 18, 19, 19, 20, 21, 22, 24, 25},  // Low
	{-1, 1, 1, 1, 2, 2, 4, 4, 4, 5, 5,  5,  8,  9,  9, 10, 10, 11, 13, 14, 16, 17, 17, 18, 20, 21, 23, 25, 26, 28, 29, 31, 33, 35, 37, 38, 40, 43, 45, 47, 49},  // Medium
	{-1, 1, 1, 2, 2, 4, 4, 6, 6, 8, 8,  8, 10, 12, 16, 12, 17, 16, 18, 21, 20, 23, 23, 25, 27, 29, 34, 34, 35, 38, 40, 43, 45, 48, 51, 53, 56, 59, 62, 65, 68},  // Quartile
	{-1, 1, 1, 2, 4, 4, 4, 5, 6, 8, 8, 11, 11, 16, 16, 18, 16, 19, 21, 25, 25, 25, 34, 30, 32, 35, 37, 40, 42, 45, 48, 51, 54, 57, 60, 63, 66, 70, 74, 77, 81},  // High
};

const vector<int> QrCode::WIDE_ALIGNMENT_PATTERN_POSITIONS_X = {
	
};

const vector<int> QrCode::WIDE_ALIGNMENT_PATTERN_POSITIONS_Y = {

};


data_too_long::data_too_long(const std::string &msg) :
	std::length_error(msg) {}

}
