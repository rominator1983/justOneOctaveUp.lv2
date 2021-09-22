/*
  Copyright 2021 rominator1983 on github
  Copyright 2006-2016 David Robillard <d@drobilla.net>
  Copyright 2006 Steve Harris <steve@plugin.org.uk>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THIS SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "lv2/core/lv2.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#define URI "http://lv2plug.in/plugins/justOneOctaveUp"

//#define FILE_LOGGING

#ifdef FILE_LOGGING
// NOTE: in the home directory
#define LOG_PATH "octaver.log"
#endif

typedef enum
{
   GAIN = 0,
   INPUT = 1,
   OUTPUT = 2
} PortIndex;

typedef enum
{
   FIRST_RUN = 0,
   RISING_EDGE = 1,
   FALLING_EDGE = 2
} PluginStatus;

typedef struct
{
   // Port buffers
   const float *gain;
   const float *input;
   float *output;
   float direction;

   // NOTE: additional ring buffer for the input
   float *inputBuffer;
   uint32_t inputBufferStart;
   uint32_t inputBufferEnd;
   uint32_t inputBufferMaxSize;
   uint32_t inputBufferLastEdgeFlip;

   PluginStatus PluginStatus;
} Octaver;

static uint32_t getBufferFilledSize(Octaver *octaver)
{
   return ((octaver->inputBufferEnd + octaver->inputBufferMaxSize) - octaver->inputBufferStart) % octaver->inputBufferMaxSize;
}

static uint32_t raiseIndex(uint32_t index, Octaver *octaver)
{
   return (index + 1) % octaver->inputBufferMaxSize;
}

static uint32_t decreaseIndex(uint32_t index, Octaver *octaver)
{
   return (index + octaver->inputBufferMaxSize - 1) % octaver->inputBufferMaxSize;
}

static void pushBuffer(Octaver *octaver, float sample)
{
   octaver->inputBuffer[octaver->inputBufferEnd] = sample;

   octaver->inputBufferEnd = raiseIndex(octaver->inputBufferEnd, octaver);
}

static float popBuffer(Octaver *octaver)
{
   float returnValue = octaver->inputBuffer[octaver->inputBufferStart];

   octaver->inputBufferStart = raiseIndex(octaver->inputBufferStart, octaver);

   return returnValue;
}

static LV2_Handle
instantiate(const LV2_Descriptor *descriptor,
            double rate,
            const char *bundle_path,
            const LV2_Feature *const *features)
{

   Octaver *octaver = (Octaver *)calloc(1, sizeof(Octaver));

#ifdef FILE_LOGGING
   FILE *pFile;
   pFile = fopen(LOG_PATH, "w+");
   fprintf(pFile, "instantiate\n");
   fclose(pFile);
#endif
   octaver->direction = 1.0f;

   // NOTE: about 0.04 second for 192000 kHz. This is enough to even octave a pure sinus 12 Hz signal
   octaver->inputBufferMaxSize = 8192;
   octaver->inputBuffer = (float *)calloc(octaver->inputBufferMaxSize, sizeof(float));

   octaver->inputBufferStart = 0;
   octaver->inputBufferEnd = 0;
   octaver->PluginStatus = FIRST_RUN;
   octaver->inputBufferLastEdgeFlip = 0;

   return (LV2_Handle)octaver;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
   Octaver *octaver = (Octaver *)instance;

   switch ((PortIndex)port)
   {
   case GAIN:
      octaver->gain = (const float *)data;
      break;
   case INPUT:
      octaver->input = (const float *)data;
      break;
   case OUTPUT:
      octaver->output = (float *)data;
      break;
   }
}

static void
activate(LV2_Handle instance)
{
}

/** Define a macro for converting a gain in dB to a coefficient. */
#define DB_CO(g) ((g) > -90.0f ? powf(10.0f, (g)*0.05f) : 0.0f)

static void
run(LV2_Handle instance, uint32_t n_samples)
{
   Octaver *octaver = (Octaver *)instance;

   const float gain = *(octaver->gain);
   const float *const input = octaver->input;
   float *const output = octaver->output;

   const float coef = DB_CO(gain);

#ifdef FILE_LOGGING
   FILE *pFile;
   pFile = fopen(LOG_PATH, "a+");

   fprintf(pFile, "\nRUN: state: %d size: %d octaver->inputBufferStart: %d octaver->inputBufferEnd: %d octaver->inputBufferLastEdgeFlip: %d\n",
           octaver->PluginStatus,
           getBufferFilledSize(octaver),
           octaver->inputBufferStart,
           octaver->inputBufferEnd,
           octaver->inputBufferLastEdgeFlip);
#endif

   uint32_t isSilence = 1;

   for (uint32_t pos = 0; pos < n_samples; pos++)
   {
      if (octaver->PluginStatus == FIRST_RUN)
      {
         octaver->PluginStatus = input[0] >= 0.0f ? RISING_EDGE : FALLING_EDGE;
      }

      float value1 = input[pos] * 0.5f * coef;
      float value2 = input[pos++] * 0.5f * coef;

      // TODO: make parameters for this
      if (value1 > 0.0001f || value1 < -0.0001f || value2 > 0.0001f || value2 < -0.0001f)
         isSilence = 0;

#ifdef FILE_LOGGING
      if (getBufferFilledSize(octaver) + 1 >= octaver->inputBufferMaxSize)
         fprintf(pFile, "Buffer overflow\n");
#endif

      // NOTE: since every half wave will be repeated (and inverted) the next half wave of the input always has to be inverted
      pushBuffer(octaver, (value1 + value2) * (octaver->PluginStatus == RISING_EDGE ? 1.0f : -1.0f));

      if (
          (octaver->PluginStatus == RISING_EDGE && (input[pos] < 0.0f || input[pos - 1] < 0.0f)) ||
          (octaver->PluginStatus == FALLING_EDGE && (input[pos] >= 0.0f || input[pos - 1] >= 0.0f)))
      {
#ifdef FILE_LOGGING
         fprintf(pFile, octaver->PluginStatus == RISING_EDGE ? "\\" : "/");
#endif
         // NOTE: copy half wave from the buffer since the last edge flip happened and put that on the buffer for future reads (further below)
         uint32_t bufferEnd = octaver->inputBufferEnd;
         while (octaver->inputBufferLastEdgeFlip != bufferEnd)
         {
#ifdef FILE_LOGGING
            fprintf(pFile, "c");
#endif
            // NOTE: always invert repeated half wave
            float value = octaver->inputBuffer[octaver->inputBufferLastEdgeFlip] * -1.0f;
            pushBuffer(octaver, value);
            octaver->inputBufferLastEdgeFlip = raiseIndex(octaver->inputBufferLastEdgeFlip, octaver);
         };
         octaver->inputBufferLastEdgeFlip = octaver->inputBufferEnd;

         // NOTE: the other direction now
         octaver->PluginStatus = octaver->PluginStatus == RISING_EDGE ? FALLING_EDGE : RISING_EDGE;
      }
   }

#ifdef FILE_LOGGING
   fprintf(pFile, "\n");
#endif

   // NOTE: read samples from the internal plugin buffer to the output
   for (uint32_t pos = 0; pos < n_samples; pos++)
   {
      if (getBufferFilledSize(octaver) > 0)
      {
#ifdef FILE_LOGGING
         fprintf(pFile, "|");
#endif
         // NOTE: mix with original signal
         output[pos] = popBuffer(octaver) * 0.75f + input[pos] * 0.25f;
      }
      else
      {
#ifdef FILE_LOGGING
         fprintf(pFile, "0");
#endif
         // NOTE: when there is no data left in the plugin buffer only the original signal is sent to the output
         // TODO: make parameters for this
         output[pos] = input[pos] * 0.25f;
      }
   }

   /*
    NOTE:   The buffer tends to build up over time (thus increasing latency) due to abnormal long half waves.
            (about 2300 samples @ 96000 have been seen for a guitar signal)
            Thus a silence detection has been added that resets the buffer values (but not clearing the buffer which is not needed) in order to get an acceptable latency.
    */
   if (isSilence)
   {
      octaver->PluginStatus = FIRST_RUN;
      octaver->inputBufferStart = 0;
      octaver->inputBufferEnd = 0;
      octaver->inputBufferLastEdgeFlip = 0;
#ifdef FILE_LOGGING
      fprintf(pFile, "\nsilence detected\n");
#endif
   }

#ifdef FILE_LOGGING
   fprintf(pFile, "\nend run\n");
   fclose(pFile);
#endif
}

static void
deactivate(LV2_Handle instance)
{
}

static void
cleanup(LV2_Handle instance)
{
   free(instance);
}

static const void *
extension_data(const char *uri)
{
   return NULL;
}

static const LV2_Descriptor descriptor = {URI,
                                          instantiate,
                                          connect_port,
                                          activate,
                                          run,
                                          deactivate,
                                          cleanup,
                                          extension_data};

LV2_SYMBOL_EXPORT
const LV2_Descriptor *
lv2_descriptor(uint32_t index)
{
   return index == 0 ? &descriptor : NULL;
}
