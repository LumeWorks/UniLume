// SPDX-License-Identifier: GPL-2.0-or-later

#include "byteio.h"
#include "pattern.h"
#include "vnconv.h"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string_view>
#include <unistd.h>

namespace {

int failures;

void expect(bool condition, std::string_view message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

class ObservableFileBIStream final : public FileBIStream {
public:
    [[nodiscard]] int descriptor() const
    {
        return fileno(m_file);
    }
};

void testBoundedWideReads()
{
    std::array<UKBYTE, 5> input{0x61, 0, 0x62, 0, 0x7f};
    std::array<UKBYTE, 32> output{};
    int inputLength = static_cast<int>(input.size());
    int outputLength = static_cast<int>(output.size());

    expect(VnConvert(CONV_CHARSET_UNIDECOMPOSED,
                     CONV_CHARSET_UNIUTF8,
                     input.data(),
                     output.data(),
                     &inputLength,
                     &outputLength) == VNCONV_NO_ERROR,
           "VnConvert accepts a bounded odd-length UCS-2 input safely");
    expect(inputLength == 1,
           "VnConvert leaves the incomplete UCS-2 byte unconsumed");

    std::array<UKBYTE, 5> unaligned{0xff, 0x34, 0x12, 0x78, 0x56};
    StringBIStream stream(unaligned.data() + 1, 4, 2);
    UKWORD word{};
    UKWORD expected{};
    std::memcpy(&expected, unaligned.data() + 1, sizeof(expected));
    expect(stream.peekNextW(word) == 1 && word == expected,
           "peekNextW supports unaligned input");
    expect(stream.getNextW(word) == 1 && word == expected,
           "getNextW supports unaligned input");

    StringBIStream shortWord(unaligned.data(), 1, 2);
    expect(shortWord.peekNextW(word) == 0 && shortWord.left() == 1,
           "peekNextW rejects a partial word without consuming it");
    shortWord.reopen();
    expect(shortWord.getNextW(word) == 0 && shortWord.left() == 1,
           "getNextW rejects a partial word without consuming it");

    UKDWORD doubleWord{};
    StringBIStream shortDoubleWord(unaligned.data(), 3, 4);
    expect(shortDoubleWord.getNextDW(doubleWord) == 0 &&
               shortDoubleWord.left() == 3,
           "getNextDW rejects a partial double word without consuming it");
}

void testOverlongPatternIsRejected()
{
    std::array<char, MAX_PATTERN_LEN + 22> pattern{};
    pattern.fill('a');
    pattern.back() = '\0';

    PatternState state{};
    state.init(pattern.data());
    bool matched = false;
    for (char ch : pattern) {
        if (ch != '\0')
            matched = matched || state.foundAtNextChar(ch);
    }
    expect(!matched, "an overlong legacy pattern is rejected safely");
}

void testTrailingViqrEscape()
{
    std::array<UKBYTE, 3> input{'a', '^', '\\'};
    std::array<UKBYTE, 16> output{};
    int inputLength = static_cast<int>(input.size());
    int outputLength = static_cast<int>(output.size());

    expect(VnConvert(CONV_CHARSET_VIQR,
                     CONV_CHARSET_UNIUTF8,
                     input.data(),
                     output.data(),
                     &inputLength,
                     &outputLength) == VNCONV_NO_ERROR,
           "VIQR conversion with a trailing escape succeeds");
    constexpr std::string_view expected = "â\\";
    expect(std::string_view(reinterpret_cast<char *>(output.data()),
                            static_cast<std::size_t>(outputLength)) == expected,
           "a trailing VIQR escape is emitted literally");
    expect(inputLength == 0, "VIQR reports the exact number of consumed bytes");
}

void testOpenedFileIsOwned()
{
    char path[] = "/tmp/unilume-byteio-XXXXXX";
    const int temporaryDescriptor = mkstemp(path);
    expect(temporaryDescriptor >= 0, "temporary input file is created");
    if (temporaryDescriptor < 0)
        return;
    close(temporaryDescriptor);

    int streamDescriptor = -1;
    {
        ObservableFileBIStream stream;
        expect(stream.open(path) == 1, "FileBIStream opens its input file");
        streamDescriptor = stream.descriptor();
    }

    errno = 0;
    expect(fcntl(streamDescriptor, F_GETFD) == -1 && errno == EBADF,
           "FileBIStream closes a file opened by the stream");
    unlink(path);
}

} // namespace

int main()
{
    testBoundedWideReads();
    testOverlongPatternIsRejected();
    testTrailingViqrEscape();
    testOpenedFileIsOwned();

    if (failures != 0) {
        std::cerr << failures << " legacy safety test(s) failed\n";
        return 1;
    }
    std::cout << "All legacy safety tests passed\n";
    return 0;
}
