#include "stdint.h"
#include "command/command.h"
#include "string.h"

static int8_t Command_PackFrame(Command_Controller *controller);

static uint32_t Command_CalculateOverHeadLength(Command_Config cfg);

static int8_t Command_ClearBuffer(Command_Controller *controller);

static int8_t Command_CheckConfig(Command_Config config);
/**
 * @arg
 * @arg pattern: search pattern.
 * @return 0=success, 1=not enough data.
 * */
static int8_t Command_ScanChars(Command_Controller *controller, char *pattern, int8_t *lps, uint8_t patternSize);

/**
 * @arg
 * @arg size: 1=8bit, 2=16bit, 3=32bit.
 * @return 0=success, 1=not enough data.
 * */
static int8_t Command_ScanUint(Command_Controller *controller, uint8_t size, uint32_t *value);

/**
 * @arg
 * @arg length: content expect length.
 * @return 0=success, 1=not enough data.
 * */
static int8_t Command_ScanContent(Command_Controller *controller, uint32_t length, uint32_t *scanedLength);

static int8_t Command_InitWorkspace(Command_Controller *controllerint8_t, Command_Config customConfig);

/**
 * @arg
 * @arg 
 * @return 0=success, -1=dismatch, 1=not enough chars
 * */
static int8_t Command_MatchChars(Command_Controller *controller, char *pattern, uint8_t size);

static void Command_ComputeNext(char *p, uint8_t M, int8_t *next);

static int8_t Command_HasAvailableLength(Command_Buffer *buffer, int32_t offset, uint32_t length);

static int8_t Command_ClearBuffer(Command_Controller *controller)
{
    Command_Buffer *lastBuffer = controller->workspace.lastBuffer;
    int32_t lastOffset = controller->workspace.lastOffset;
    lastBuffer->size = lastOffset + 1;
    controller->bufferTail = lastBuffer;

    while (lastBuffer != 0)
    {
        Command_Buffer *nextBuffer = lastBuffer->nextBuffer;
        Command_Mrelease(controller, lastBuffer->data);
        Command_Mrelease(controller, lastBuffer);
        lastBuffer = nextBuffer;
    }

    return 0;
}

static inline uint32_t Command_CalculateOverHeadLength(Command_Config cfg)
{
    return (cfg.lengthIncludePrefix ? 0 : (uint32_t)(cfg.prefixFieldSize))                                                // prefix length
           + (cfg.lengthIncludeSuffix ? 0 : (uint32_t)(cfg.suffixFieldSize))                                              // suffix length
           + (cfg.lengthIncludeLength ? 0 : (cfg.lengthFieldSize == 0 ? 0 : (uint32_t)(1 << (cfg.lengthFieldSize - 1)))); // length
}

static int8_t Command_PackFrame(Command_Controller *controller)
{
    Command_Buffer *startBuffer = controller->workspace.startBuffer;
    int32_t startOffset = controller->workspace.startOffset;
    Command_Buffer *lastBuffer = controller->workspace.lastBuffer;
    int32_t lastOffset = controller->workspace.startOffset;

    Command_Frame *frame = (Command_Frame *)Command_Malloc(controller, sizeof(Command_Frame));
    frame->length = controller->workspace.currentContentLength + Command_CalculateOverHeadLength(controller->workspace.config);
    frame->startBuffer = startBuffer;
    frame->startOffset = startOffset;
    frame->lastBuffer = lastBuffer;
    frame->lastOffset = lastOffset;

    while (startBuffer != 0)
    {
        (startBuffer->refCount)++; // Add reference count.
        if (startBuffer != lastBuffer)
        {
            startBuffer->completed = 1; // Set to true, if all of the buffer content has been packed. Other words, set to true except last buffer.
            startBuffer = startBuffer->nextBuffer;
        }
        else
        {
            break;
        }
    }

    if (controller->pendingFramesTail == 0)
    {
        controller->pendingFramesTail = frame;
        controller->pendingFramesHead = frame;
    }
    else
    {
        controller->pendingFramesTail->nextFrame = frame;
        controller->pendingFramesTail = frame;
    }

    return 0;
}

static void Command_ComputeNext(char *p, uint8_t M, int8_t *next)
{
    next[0] = -1;
    int i = 0, j = -1;

    while (i < M)
    {
        if (j == -1 || p[i] == p[j])
        {
            ++i;
            ++j;
            next[i] = j;
        }
        else
        {
            j = next[j];
        }
    }
}

static int8_t Command_CheckConfig(Command_Config config)
{
    if (config.lengthFieldSize == 0) // variable length
    {
        if (config.suffixFieldSize == 0)
        {
            return COMMAND_CONFIG_ERROR_VAR_LENGTH_MUST_HAVE_SUFFIX;
        }
    }
    else // fixed length
    {
        if (config.prefixFieldSize == 0)
        {
            return COMMAND_CONFIG_ERROR_FIXED_LENGTH_MUST_HAVE_PREFIX;
        }
    }

    return 0;
}

static int8_t Command_InitWorkspace(Command_Controller *controller, Command_Config config)
{
    controller->workspace.expectContentLength = 0;
    controller->workspace.startBuffer = 0;
    controller->workspace.startOffset = -1;
    controller->workspace.currentContentLength = 0;
    controller->workspace.config = config;
    controller->workspace.stage = Command_PARSE_STAGE_INIT;
    return 0;
}
static inline int8_t Command_ScanContent(Command_Controller *controller, uint32_t expectLength, uint32_t *scanedLength)
{
    Command_Buffer *buffer = controller->workspace.lastBuffer;
    int32_t offset = controller->workspace.lastOffset;

    controller->workspace.segmentStartBuffer = buffer;
    controller->workspace.segmentStartOffset = offset;

    uint32_t remainLength = expectLength;

    uint32_t emptySize = buffer->size - offset - 1;
    while (remainLength > emptySize)
    {
        remainLength -= emptySize;

        if (buffer->nextBuffer == 0)
        {
            controller->workspace.lastBuffer = buffer;
            controller->workspace.lastOffset = buffer->size - 1;
            *scanedLength = expectLength - remainLength;
            return 1;
        }
        else
        {
            buffer = buffer->nextBuffer;
            offset = -1;
            emptySize = buffer->size;
        }
    }

    controller->workspace.lastBuffer = buffer;
    controller->workspace.lastOffset = remainLength - 1;

    *scanedLength = expectLength;

    return 0;
}

static inline int8_t Command_HasAvailableLength(Command_Buffer *lastBuffer, int32_t lastOffset, uint32_t length)
{
    int32_t expectLastOffset = lastOffset + length;
    do
    {
        if (expectLastOffset < lastBuffer->size) // offset point to last parsed position, so this place should compare by <
        {
            return 0;
        }
        expectLastOffset -= lastBuffer->size;
        lastBuffer = lastBuffer->nextBuffer;
    } while (lastBuffer != 0);

    return 1;
}

static int8_t Command_ScanUint(Command_Controller *controller, uint8_t size, uint32_t *value)
{
    Command_Buffer *buffer = controller->workspace.lastBuffer;
    int32_t offset = controller->workspace.lastOffset;
    if (Command_HasAvailableLength(buffer, offset, size) != 0)
    {
        return 1;
    }

    controller->workspace.segmentStartBuffer = buffer;
    controller->workspace.segmentStartOffset = offset;

    uint32_t tmpValue = 0;
    while (size-- > 0)
    {
        offset++;
        if (offset == buffer->size)
        {
            buffer = buffer->nextBuffer;
            offset = 0;
        }
        tmpValue = (tmpValue << 8) + buffer->data[offset];
    }
    *value = tmpValue;

    controller->workspace.lastBuffer = buffer;
    controller->workspace.lastOffset = offset;
    return 0;
}

static int8_t Command_ScanChars(Command_Controller *controller, char *p, int8_t *next, uint8_t pSize)
{
    Command_Buffer *curBuffer = controller->workspace.lastBuffer;
    int32_t curOffset = controller->workspace.lastOffset;

    char *curData = curBuffer->data;
    uint32_t curSize = curBuffer->size;

    Command_Buffer *matchedHeadBuffer = curBuffer; // point to the previous position of she current matched head
    uint32_t matchedHeadSize = matchedHeadBuffer->size;
    int32_t matchedHeadOffset = curOffset;

    int j = -1;

    do
    {
        if (curOffset == curSize) // check first for the possibility of go back to last parsed position
        {
            if (curBuffer->nextBuffer == 0)
            {
                controller->workspace.lastBuffer = matchedHeadBuffer;
                controller->workspace.lastOffset = matchedHeadOffset;
                return 1;
            }
            else
            {
                curBuffer = curBuffer->nextBuffer;
                curOffset = 0;
                curData = curBuffer->data;
                curSize = curBuffer->size;
            }
        }

        if (j == -1 /* should match frist char */ || curData[curOffset] == p[j])
        {
            j++;
            curOffset++;
        }
        else
        {
            matchedHeadOffset += j - next[j];
            while (matchedHeadOffset >= matchedHeadSize)
            {
                // matchHead point to current match head, can not been overflowed;
                matchedHeadOffset -= matchedHeadSize;
                matchedHeadBuffer = matchedHeadBuffer->nextBuffer;
                matchedHeadSize = matchedHeadBuffer->size;
            }
            j = next[j];
        }
    } while (j < pSize);

    curOffset--; // back to last parsed position
    controller->workspace.lastBuffer = curBuffer;
    controller->workspace.lastOffset = curOffset;

    controller->workspace.segmentStartBuffer = matchedHeadBuffer;
    controller->workspace.segmentStartOffset = matchedHeadOffset;

    return 0;
}

/**
 * @arg
 * @arg 
 * @return 0=success, -1=dismatch, 1=not enough chars
 * */
static int8_t Command_MatchChars(Command_Controller *controller, char *pattern, uint8_t size)
{
    Command_Buffer *buffer = controller->workspace.lastBuffer;
    int32_t offset = controller->workspace.lastOffset;

    controller->workspace.segmentStartBuffer = buffer;
    controller->workspace.segmentStartOffset = offset;

    if (Command_HasAvailableLength(buffer, offset, size) != 0)
    {
        return 1;
    }
    for (uint8_t i = 0; i < size; i++)
    {
        offset++;
        if (offset == buffer->size)
        {
            buffer = buffer->nextBuffer;
            offset = 0;
        }
        if (buffer->data[offset] != pattern[i])
        {
            return -1;
        }
    }

    controller->workspace.lastBuffer = buffer;
    controller->workspace.lastOffset = offset;

    return 0;
}

int8_t Command_Init(Command_Controller *controller, Command_Config cfg, char *name, int8_t (*bufferAppendCallback)(struct Command_Controller *controller), void *outerState)
{
    uint8_t checkResult = Command_CheckConfig(cfg);
    if (checkResult != 0)
    {
        return checkResult;
    }
    controller->config = cfg;
    controller->outerState = outerState;
    controller->BufferAppendCallback = bufferAppendCallback;

    if (cfg.prefixFieldSize != 0)
    {
        int8_t *lps = Command_Malloc(controller, cfg.prefixFieldSize);
        Command_ComputeNext(cfg.prefixChars, cfg.prefixFieldSize, lps);
        controller->prefixNexts = lps;
    }
    if (cfg.suffixFieldSize != 0)
    {
        int8_t *lps = Command_Malloc(controller, cfg.suffixFieldSize);
        Command_ComputeNext(cfg.suffixChars, cfg.suffixFieldSize, lps);
        controller->suffixNexts = lps;
    }

    Command_Buffer *initBuffer = Command_Malloc(controller, sizeof(Command_Buffer));
    initBuffer->data = Command_Malloc(controller, 1);
    initBuffer->size = 1;
    controller->bufferHead = initBuffer;
    controller->bufferTail = initBuffer;
    controller->workspace.lastBuffer = initBuffer;
    controller->workspace.lastOffset = 0;

    return 0;
}

void Command_AppendBuffer(Command_Controller *controller, char *data, uint32_t size)
{
    char *dataPtr = Command_Malloc(controller, size);
    memcpy(dataPtr, data, (int)size);
    Command_Buffer *bufPtr = (Command_Buffer *)Command_Malloc(controller, sizeof(Command_Buffer));
    bufPtr->data = dataPtr;
    bufPtr->size = size;
    bufPtr->refCount = 0;
    bufPtr->completed = 0;

    controller->bufferTail->nextBuffer = bufPtr;
    controller->bufferTail = bufPtr;

    if (controller->BufferAppendCallback != 0)
    {
        controller->BufferAppendCallback(controller);
    }
}

int8_t Command_Parse(Command_Controller *controller, Command_Config *customConfig)
{
    uint8_t stage = controller->workspace.stage;
    uint8_t frameCount = 0;
    Command_Config config = controller->workspace.config;

    if (customConfig != 0)
    {
        config = *customConfig;
        controller->workspace.lastBuffer = controller->workspace.segmentStartBuffer;
        controller->workspace.lastOffset = controller->workspace.segmentStartOffset;
        Command_ClearBuffer(controller);
        stage = Command_PARSE_STAGE_SEEKING_PREFIX;
    }

    while (1)
    {
        int8_t result = 0;
        switch (stage)
        {
        case Command_PARSE_STAGE_INIT:
            config = controller->config;
            result = Command_InitWorkspace(controller, config);
            if (result != 0)
            {
                stage = Command_PARSE_STAGE_INIT;
                break;
            }
        case Command_PARSE_STAGE_SEEKING_PREFIX:
            if (config.lengthIncludePrefix != 0)
            {
                result = Command_ScanChars(controller, config.prefixChars, controller->prefixNexts, config.prefixFieldSize);
                if (result != 0)
                {
                    stage = Command_PARSE_STAGE_SEEKING_PREFIX;
                    break;
                }
                else
                {
                    if (controller->workspace.startBuffer == 0)
                    {
                        controller->workspace.startBuffer = controller->workspace.segmentStartBuffer;
                        controller->workspace.startOffset = controller->workspace.segmentStartOffset;
                    }
                }
            }
        case Command_PARSE_STAGE_SEEKING_LENGTH:
            if (config.lengthFieldSize != 0)
            {
                uint32_t expectLength = 0;
                result = Command_ScanUint(controller, config.lengthFieldSize, &expectLength);
                if (result != 0)
                {
                    stage = Command_PARSE_STAGE_SEEKING_LENGTH;
                    break;
                }
                else
                {
                    controller->workspace.expectContentLength = expectLength - Command_CalculateOverHeadLength(config);
                    if (controller->workspace.startBuffer == 0)
                    {
                        controller->workspace.startBuffer = controller->workspace.segmentStartBuffer;
                        controller->workspace.startOffset = controller->workspace.segmentStartOffset;
                    }
                }
            }
        case Command_PARSE_STAGE_SEEKING_CONTENT:
            if (config.lengthFieldSize != 0)
            {
                uint32_t parsedLength = 0;
                result = Command_ScanContent(controller, controller->workspace.expectContentLength, &parsedLength);
                controller->workspace.currentContentLength += parsedLength;
                if (result != 0)
                {
                    stage = Command_PARSE_STAGE_SEEKING_CONTENT;
                    break;
                }
                else
                {
                    if (controller->workspace.startBuffer == 0)
                    {
                        controller->workspace.startBuffer = controller->workspace.segmentStartBuffer;
                        controller->workspace.startOffset = controller->workspace.segmentStartOffset;
                    }
                }
            }
        case Command_PARSE_STAGE_MATCHING_SUFFIX:
            if (config.lengthFieldSize != 0 && config.suffixFieldSize != 0)
            {
                result = Command_MatchChars(controller, config.suffixChars, config.suffixFieldSize);
                if (result == 0)
                {
                    if (controller->workspace.startBuffer == 0)
                    {
                        controller->workspace.startBuffer = controller->workspace.segmentStartBuffer;
                        controller->workspace.startOffset = controller->workspace.segmentStartOffset;
                    }
                }
                else if (result == 1) // not enough buf
                {
                    stage = Command_PARSE_STAGE_MATCHING_SUFFIX;
                    break;
                }
                else // mismatch
                {
                    stage = Command_PARSE_STAGE_ABORT;
                    break;
                }
            }
        case Command_PARSE_STAGE_SEEKING_SUFFIX:
            if (config.lengthFieldSize == 0 && config.suffixFieldSize != 0)
            {
                result = Command_ScanChars(controller, config.suffixChars, controller->suffixNexts, config.suffixFieldSize);
                if (result != 0)
                {
                    stage = Command_PARSE_STAGE_SEEKING_SUFFIX;
                    break;
                }
                else
                {
                    if (controller->workspace.startBuffer == 0)
                    {
                        controller->workspace.startBuffer = controller->workspace.segmentStartBuffer;
                        controller->workspace.startOffset = controller->workspace.segmentStartOffset;
                    }
                }
            }

        case Command_PARSE_STAGE_DONE:
            result = Command_PackFrame(controller);
            if (result == 0)
            {
                stage = Command_PARSE_STAGE_INIT;

                frameCount++;
                break;
            }

        case Command_PARSE_STAGE_ABORT:

            controller->workspace.lastBuffer = controller->workspace.segmentStartBuffer;
            controller->workspace.lastOffset = controller->workspace.segmentStartOffset;
            stage = Command_PARSE_STAGE_INIT;
        default:
            stage = Command_PARSE_STAGE_INIT;
            break;
        }

        if (result == 1)
        {
            // not enough data, exit and wait for next buffer.
            controller->workspace.stage = stage;
            break;
        }
        else // everything is ok.
        {
        }
    }

    return frameCount;
}

Command_Frame *Command_PickFrame(Command_Controller *controller)
{
    Command_Frame *frame = controller->pendingFramesHead;
    if (frame == 0)
    {
        return 0;
    }
    controller->pendingFramesHead = frame->nextFrame;
    if (controller->pendingFramesHead == 0)
    {
        controller->pendingFramesTail = 0;
    }
    return frame;
}

void Command_ReleaseFrame(Command_Controller *controller, Command_Frame *frame)
{
    Command_Buffer *buffer = frame->startBuffer;

    while (buffer != 0)
    {
        (buffer->refCount)--; // Add reference count.

        if (buffer == frame->lastBuffer)
        {
            break;
        }
    }

    Command_Mrelease(controller, frame);

    buffer = controller->bufferHead;
    while (buffer != 0)
    {
        Command_Buffer *nextBuffer = buffer->nextBuffer;
        if (buffer->refCount == 0 && buffer->completed)
        {
            Command_Mrelease(controller, buffer->data);
            Command_Mrelease(controller, buffer);
        }
        else
        {
            controller->bufferHead = buffer;
            break;
        }
        buffer = nextBuffer;
    }
}

int8_t Command_ClearFrame(Command_Controller *controller)
{
    Command_Frame *frame = controller->pendingFramesHead;
    Command_Buffer *buffer = 0;
    while (frame != 0)
    {
        buffer = frame->startBuffer;

        while (buffer != 0)
        {
            (buffer->refCount)--; // Add reference count.

            if (buffer == frame->lastBuffer)
            {
                break;
            }
        }
        Command_Frame *nextFrame = frame->nextFrame;
        Command_Mrelease(controller, frame);
        frame = nextFrame;
    }

    buffer = controller->bufferHead;
    while (buffer != 0)
    {
        Command_Buffer *nextBuffer = buffer->nextBuffer;
        if (buffer->refCount == 0 && buffer->completed)
        {
            Command_Mrelease(controller, buffer->data);
            Command_Mrelease(controller, buffer);
        }
        else
        {
            controller->bufferHead = buffer;
            break;
        }
        buffer = nextBuffer;
    }

    return 0;
}

uint32_t Command_ExtractFrame(Command_Frame *frame, uint32_t startPos, uint32_t length, char *dist)
{
    Command_Buffer *buffer = frame->startBuffer;
    if (length == 0 || (startPos + length) > frame->length)
    {
        length = frame->length - startPos; // Length==0 means copy to the end of the frame; If length out of set end of the frame, just copy available length;
    }

    uint32_t startIndex = frame->startOffset + 1;

    startIndex += startPos;

    while (startIndex >= buffer->size)
    {
        buffer = buffer->nextBuffer;
        startIndex -= buffer->size;
    }
    uint32_t remainLength = length;
    while (remainLength > 0)
    {
        uint32_t copySize = remainLength > (buffer->size - startIndex) ? (buffer->size - startIndex) : remainLength;
        memcpy(dist, (buffer->data) + startIndex, copySize);
        remainLength -= copySize;
        dist += copySize;
        buffer = buffer->nextBuffer;
        startIndex = 0;
    }

    return length;
}