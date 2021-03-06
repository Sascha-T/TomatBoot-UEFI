/** @file
  SpeculationBarrier() function for IA32 and x64.

  Copyright (C) 2018 - 2019, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "BaseLibInternals.h"
#include <Library/BaseLib.h>

/**
  Uses as a barrier to stop speculative execution.

  Ensures that no later instruction will execute speculatively, until all prior
  instructions have completed.

**/
VOID
EFIAPI
SpeculationBarrier (
  VOID
  )
{
  AsmLfence ();
}
