
#include <stdint.h>

/**
기본 Type정의.
*/
#define FF32			0xFFFFFFFF
#define FF16			0xFFFF
#define FF08			0xFF

typedef uint32_t			uint32;
typedef int32_t				int32;
typedef uint16_t			uint16;
typedef int16_t				int16;
typedef uint8_t				uint8;
typedef int8_t				int8;
typedef uint64_t			uint64;
typedef int64_t				int64;

typedef void (*Cbf)(uint32 tag, uint32 result);
