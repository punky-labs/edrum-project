#pragma once
#include <Arduino.h>

// ===========================================================================
// FastWriteFIFO + update_fifo
// ===========================================================================
// Ported verbatim from Edrumulus (Volker Fischer, GPL-2.0):
//   D:\Dev\E-drums\eDrumulus\edrumulus-src\edrumulus\edrumulus.h
// Used by the PDrum2 power-domain detector (Stage 2). FastWriteFIFO is a ring
// buffer with cheap add() and operator[] indexing relative to the write pointer;
// update_fifo() is a small shift-register helper for the short IIR histories.
// ---------------------------------------------------------------------------

inline void update_fifo ( const float input,
                          const int   fifo_length,
                          float*      fifo_memory )
{
  // move all values in the history one step back and put new value on the top
  for ( int i = 0; i < fifo_length - 1; i++ )
  {
    fifo_memory[i] = fifo_memory[i + 1];
  }
  fifo_memory[fifo_length - 1] = input;
}

inline void allocate_initialize ( float**   array_memory,
                                  const int array_length )
{
  // (delete and) allocate memory
  if ( *array_memory != nullptr )
  {
    delete[] *array_memory;
  }

  *array_memory = new float[array_length];

  // initialization values
  for ( int i = 0; i < array_length; i++ )
  {
    ( *array_memory )[i] = 0.0f;
  }
}

class FastWriteFIFO
{
public:
  void initialize ( const int len )
  {
    pointer     = 0;
    fifo_length = len;
    allocate_initialize ( &fifo_memory, len );
  }

  void add ( const float input )
  {
    // write new value and increment data pointer with wrap around
    fifo_memory[pointer] = input;
    pointer              = ( pointer + 1 ) % fifo_length;
  }

  const float operator[] ( const int index )
  {
    return fifo_memory[( pointer + index ) % fifo_length];
  }

protected:
  float* fifo_memory = nullptr;
  int    pointer     = 0;
  int    fifo_length = 1;
};
