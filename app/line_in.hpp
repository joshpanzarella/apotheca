#pragma once
// One line of a song file read word by word and number by number, as a std::istringstream of
// that line reads it (the classic "C" locale: the program never sets another), without making
// one: a stream a line, and `>>` for every number, were 35% of reading a song back (speed plan
// batch 4). Only what song_file.cpp asks of a line is here, with the stream's own outcome for
// each, failure included:
//
//   in >> number     a double as libstdc++'s num_get reads one: its characters gathered by the
//                    same rules (a sign, the first of any leading zeros, digits, one point, an
//                    e with its sign, only after a digit), then strtod. Nothing that reads:
//                    0 and failed; too big: the largest double, signed, and failed. At the end
//                    of the line, or after a failure: failed and the number left as it was.
//   in >> word       the next run of characters that are not white space.
//   restOfLine(in)   std::getline(in, s): the rest of the line, as it is.
//   skipSpace(in)    in >> std::ws.
//   in.clear()       as a stream's: the failure forgotten.
//
// save_test (testALineReadsAsAStreamReadsIt) holds it to std::istringstream over many lines.

#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace apotheca {

class LineIn {
public:
    LineIn(const char* begin, const char* end) : at_(begin), end_(end) {}
    explicit operator bool() const { return !fail_; }
    bool operator!() const { return fail_; }
    void clear() { fail_ = eof_ = false; }

    static bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }

    LineIn& operator>>(std::string& word) {
        if (!sentry()) return *this;
        const char* from = at_;
        while (at_ < end_ && !space(*at_)) ++at_;
        word.assign(from, static_cast<std::size_t>(at_ - from));
        if (at_ == end_) eof_ = true;
        return *this;
    }

    LineIn& operator>>(double& v) {
        if (!sentry()) return *this;
        // The characters num_get gathers (libstdc++'s _M_extract_float, in the "C" locale).
        char small[64];
        std::string big;
        std::size_t n = 0;
        const auto add = [&](char c) {
            if (n < sizeof small - 1) small[n++] = c;
            else {
                if (big.empty()) big.assign(small, n);
                big += c;
                ++n;
            }
        };
        if (at_ < end_ && (*at_ == '+' || *at_ == '-')) add(*at_++);
        bool mantissa = false, point = false, sci = false;
        while (at_ < end_ && *at_ == '0') { // leading zeros: the first is kept
            if (!mantissa) add('0'), mantissa = true;
            ++at_;
        }
        while (at_ < end_) {
            const char c = *at_;
            if (c >= '0' && c <= '9') {
                add(c);
                mantissa = true;
            } else if (c == '.' && !point && !sci) {
                add('.');
                point = true;
            } else if ((c == 'e' || c == 'E') && !sci && mantissa) {
                add('e');
                sci = true;
                if (++at_ == end_) break;
                if (*at_ == '+' || *at_ == '-') add(*at_);
                else continue; // (looked at again, not passed)
            } else {
                break;
            }
            ++at_;
        }
        if (at_ == end_) eof_ = true;
        const char* text = n < sizeof small - 1 ? (small[n] = 0, small) : big.c_str();
        double read = 0.0;
        if (exactly(text, read)) { // (the usual numbers: no strtod)
            v = read;
            return *this;
        }
        char* stop = nullptr;
        read = std::strtod(text, &stop);
        if (stop == text || *stop != '\0') {
            v = 0.0;
            fail_ = true;
        } else if (read == std::numeric_limits<double>::infinity()) {
            v = std::numeric_limits<double>::max();
            fail_ = true;
        } else if (read == -std::numeric_limits<double>::infinity()) {
            v = -std::numeric_limits<double>::max();
            fail_ = true;
        } else {
            v = read;
        }
        return *this;
    }

    friend void skipSpace(LineIn& in) {
        while (in.at_ < in.end_ && space(*in.at_)) ++in.at_;
        if (in.at_ == in.end_) in.eof_ = true;
    }
    friend void restOfLine(LineIn& in, std::string& rest) {
        if (in.fail_ || in.eof_) {
            in.fail_ = true;
            return;
        }
        rest.assign(in.at_, static_cast<std::size_t>(in.end_ - in.at_));
        if (rest.empty()) in.fail_ = true;
        in.at_ = in.end_;
        in.eof_ = true;
    }

private:
    // What strtod gives, for text of at most 15 digits whose point and e put it within 22 tens of a
    // whole number: that whole number and the power of ten are both exact doubles, so one multiply or
    // divide is the nearest double, as strtod's (to nearest, the "fast path" every strtod takes). False
    // for anything else: strtod reads it. `text` is what operator>> gathered.
    static bool exactly(const char* text, double& v) {
        static const double kTensUp[23] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15,
                                           1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
        const char* p = text;
        const bool minus = *p == '-';
        if (*p == '+' || *p == '-') ++p;
        unsigned long long whole = 0;
        int digits = 0, tens = 0;
        bool any = false;
        for (; *p >= '0' && *p <= '9'; ++p, any = true)
            if (whole || *p != '0') {
                if (++digits > 15) return false;
                whole = whole * 10 + static_cast<unsigned>(*p - '0');
            }
        if (*p == '.') {
            for (++p; *p >= '0' && *p <= '9'; ++p, any = true) {
                --tens;
                if (whole || *p != '0') {
                    if (++digits > 15) return false;
                    whole = whole * 10 + static_cast<unsigned>(*p - '0');
                }
            }
        }
        if (!any) return false;
        if (*p == 'e') {
            ++p;
            const bool down = *p == '-';
            if (*p == '+' || *p == '-') ++p;
            if (!(*p >= '0' && *p <= '9')) return false;
            int e = 0;
            for (; *p >= '0' && *p <= '9'; ++p) {
                if (e > 1000) return false;
                e = e * 10 + (*p - '0');
            }
            tens += down ? -e : e;
        }
        if (*p != '\0' || tens > 22 || tens < -22) return false;
        const double m = static_cast<double>(whole); // exact: 15 digits at most
        v = tens >= 0 ? m * kTensUp[tens] : m / kTensUp[-tens];
        if (minus) v = -v;
        return true;
    }

    // A stream's sentry for `>>`: nothing if it is not good; white space skipped; the end: failed.
    bool sentry() {
        if (fail_ || eof_) {
            fail_ = true;
            return false;
        }
        while (at_ < end_ && space(*at_)) ++at_;
        if (at_ == end_) {
            eof_ = fail_ = true;
            return false;
        }
        return true;
    }

    const char* at_;
    const char* end_;
    bool fail_ = false, eof_ = false;
};

} // namespace apotheca
