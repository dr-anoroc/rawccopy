#ifndef REGEX_H
#define REGEX_H

// Simple regex matching (based on Kernighan and Pike),
// but tuned for our purposes:
// - matching is case insensitive
// - added escaping and digit character class
// - it returns start and end of the match

// c matches any literal character c
// . matches any single character
// ^ matches the beginning of the input string
// $ matches the end of the input string
// * matches zero or more occurrences of the previous character
// \ followed by any of . ^ $ * escapes it, ie matches the
//   character itself
// \d matched any digit
// \ followed by any other character is interpreted as \

char* match(const char* regexp, const char* text, char** end);

#endif // !REGEX_H
