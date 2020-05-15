// Macro magic
#ifndef MAGIC_H
#define MAGIC_H

// Paste up to 8 tokens
#define P(...) P2(__VA_ARGS__,,,,,,,,,,)
#define P2(a,b,c,d,e,f,g,h,i,j,...) a##b##c##d##e##f##g##h##i##j

// Paste up to 8 tokens then string-ize
#define PS(...) PS2(P(__VA_ARGS__))
#define PS2(...) PS3(__VA_ARGS__)
#define PS3(token) #token

#endif

