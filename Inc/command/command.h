// #ifndef __WINDWOLF_COMMAND_H_
// #define __WINDWOLF_COMMAND_H_

#define COMMAND_CONFIG_ERROR_VAR_LENGTH_MUST_HAVE_SUFFIX 1;
#define COMMAND_CONFIG_ERROR_FIXED_LENGTH_MUST_HAVE_PREFIX 2;

#define Command_PARSE_STAGE_INIT 0U
#define Command_PARSE_STAGE_SEEKING_PREFIX 10U
#define Command_PARSE_STAGE_SEEKING_LENGTH 20U
#define Command_PARSE_STAGE_SEEKING_CONTENT 30U
#define Command_PARSE_STAGE_MATCHING_SUFFIX 40U
#define Command_PARSE_STAGE_SEEKING_SUFFIX 41U
#define Command_PARSE_STAGE_DONE 50U
#define Command_PARSE_STAGE_ABORT 100U

typedef struct Command_Config
{

    uint8_t prefixFieldSize : 3; // prefix length. 0-7, 0=none.
    uint8_t suffixFieldSize : 3; // suffix length. 0-7, 0=none.
    uint8_t lengthFieldSize : 2; // length. 0=none, 1=8bit, 2=16bit, 3=32bit.
    uint8_t lengthIncludePrefix : 1;
    uint8_t lengthIncludeSuffix : 1;
    uint8_t lengthIncludeLength : 1;
    char *prefixChars;
    char *suffixChars;
} Command_Config;

typedef struct Command_Buffer
{
    struct Command_Buffer *nextBuffer;
    char *data;
    uint32_t size;
    uint8_t completed : 1;
    uint8_t refCount : 7;
} Command_Buffer;

typedef struct Command_Frame
{
    struct Command_Frame *nextFrame;
    Command_Buffer *startBuffer; // Pointer to the previous position of the actual start position.
    int32_t startOffset;        // Pointer to the previous position of the actual start position.
    Command_Buffer *lastBuffer;  // Pointer to the position of the end.
    int32_t lastOffset;         // Pointer to the position of the end.
    uint32_t length;            // Represent the total length of the frame, include prefix, length, content, suffix.

} Command_Frame;

typedef struct Command_Workspace
{

    Command_Buffer *startBuffer;    // Pointer to the previous position of the frame's buffer start position.
    int32_t startOffset;           // Pointer to the previous position of the frame's buffer start position.
    uint32_t expectContentLength;  // The value represents content length that been parsed, if block has fixed length.
    uint32_t currentContentLength; // Frame content length that has been parsed.

    Command_Buffer *lastBuffer; // next read buffer. set by buffer append, read and move forward by parse stage.
    int32_t lastOffset;        // next read postion.

    Command_Buffer *segmentStartBuffer; //
    int32_t segmentStartOffset;        //

    uint8_t stage;

    Command_Config config;

} Command_Workspace;

typedef struct Command_Controller
{
    Command_Config config;
    Command_Frame *pendingFramesHead;
    Command_Frame *pendingFramesTail;
    void *outerState;
    Command_Buffer *bufferHead;
    Command_Buffer *bufferTail;
    Command_Workspace workspace;
    int8_t *prefixNexts;
    int8_t *suffixNexts;
    int8_t (*BufferAppendCallback)(struct Command_Controller *controller);
} Command_Controller;

int8_t Command_Init(Command_Controller *controller, Command_Config cfg, char *name, int8_t (*bufferAppendCallback)(struct Command_Controller *controller), void *outerState);

void *Command_Malloc(Command_Controller *controller, uint32_t size);
void Command_Mrelease(Command_Controller *controller, void *ptr);

void Command_AppendBuffer(Command_Controller *controller, char *data, uint32_t size);

int8_t Command_Parse(Command_Controller *controller, Command_Config *customConfig);

Command_Frame *Command_PickFrame(Command_Controller *controller);

void Command_ReleaseFrame(Command_Controller *controller, Command_Frame *frame);

int8_t Command_ClearFrame(Command_Controller *controller);

uint32_t Command_ExtractFrame(Command_Frame *frame, uint32_t startPos, uint32_t length, char *dist);

// #endif //__WINDWOLF_COMMAND_H_
