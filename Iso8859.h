// default map for charsets that are highly "non-latin"
extern const unsigned char map_8859_default[256];
// Latin-1
// Adapted from Wikipedia:
// Albanian, Basque, Catalan, Danish, Dutch (missing some letters),
// English, Estonian (missing some letters), Faroese,
// French (missing some letters), Finnish (missing some letters),
// Galician, German, Icelandic, Irish (new orthography), Italian,
// Latin, Norwegian, Portuguese, Rhaeto-Romanic, Scottish, Spanish,
// Swedish, Afrikaans, Swahili
extern const unsigned char map_8859_1[256];
// Latin-2
// Adapted from Wikipedia:
// Bosnian, Croatian, Czech, Hungarian, Polish, Romainian, Serbian,
// Serbocroatian, Slovak, Slovenian, Upper Sorbian and Lower Sorbian
extern const unsigned char map_8859_2[256];
// Latin-3 (South European)
// Adapted from Wikipedia:
// Turkish (superceded by 8859-9), Maltese, Esperanto
extern const unsigned char map_8859_3[256];
// Latin-4 (North European)
// Adapted from Wikipedia:
// Estonian, Latvian, Lithuanian, Greenlandic, and Sami
extern const unsigned char map_8859_4[256];
// Latin-5 (Turkish)
// Adapted from Wikipedia:
// Turkish
extern const unsigned char map_8859_9[256];
// Latin-6 (Nordic)
// Adapted from Wikipedia
extern const unsigned char map_8859_10[256];
// Latin-7 (Baltic Rim)
// Adapted from Wikipedia:
extern const unsigned char map_8859_13[256];
// Latin-8 (Celtic)
// Adapted from Wikipedia:
// Gaelic, Welsh, Breton
extern const unsigned char map_8859_14[256];
// Latin-9
// Adapted from Wikipedia:
// Update of 8859-1
// English, French, German, Spanish and Portuguese
extern const unsigned char map_8859_15[256];
// Latin-10 "South-Eastern European"
// Adapted from Wikipedia:
// Albanian, Croatian, Hungarian, Polish, Romanian and Slovenian, French,
// Italian and Irish Gaelic (new orthography).
extern const unsigned char map_8859_16[256];
// array of all 8859 charsets
#define ISO_8859_NUM_CHARSETS 16
extern const unsigned char* map_8859[ISO_8859_NUM_CHARSETS + 1];
// MS-WIN codepage 1252
#define WIN_125X_NUM_CHARSETS 5 
extern const unsigned char* map_win_125x[WIN_125X_NUM_CHARSETS + 1];
