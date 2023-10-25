#pragma once

#include "LifeAPI.h"
#include "Bits.hpp"

struct PropagateResult {
  bool consistent;
  bool changed;
  bool edgesChanged;
};

class LifeStableState {
public:
  LifeState state;
  LifeState stateZOI;
  LifeState unknownStable;
  LifeState glanced; // Glanced cells are OFF cells that have at most one ON neighbour
  LifeState glancedON;  // GlancedON cells are OFF cells have at least two ON neighbours

  // Neighbour counts in binary
  LifeState state2;
  LifeState state1;
  LifeState state0;

  LifeState unknown3;
  LifeState unknown2;
  LifeState unknown1;
  LifeState unknown0;

  // Must be unknown previously!
  void SetCell(std::pair<int, int> cell, bool which);

  // NOTE: doesn't update the counts
  PropagateResult PropagateColumnStep(int column);
  void UpdateZOIColumn(int column);
  PropagateResult PropagateColumn(int column);
  // bool PropagateColumn(int column);

  PropagateResult PropagateStableStep();
  PropagateResult PropagateStable();

  std::pair<int, int> UnknownNeighbour(std::pair<int, int> cell) const;

  PropagateResult TestUnknowns(const LifeState &cells);
  PropagateResult TestUnknownNeighbourhood(std::pair<int, int> cell);
  PropagateResult TestUnknownNeighbourhoods(const LifeState &cells);
  bool CompleteStableStep(std::chrono::system_clock::time_point &timeLimit, bool minimise, unsigned &maxPop, LifeState &best);
  LifeState CompleteStable(unsigned timeout, bool minimise);

  LifeState Vulnerable() const;
};

void LifeStableState::SetCell(std::pair<int, int> cell, bool which) {
  state.SetCellUnsafe(cell, which);
  unknownStable.Erase(cell);

  const std::array<std::pair<int, int>, 9> neighbours = LifeState::NeighbourhoodCells(cell);
  for (auto n : neighbours) {
    // Decrement unknown counts
    if (unknown0.Get(n)) {
      unknown0.Erase(n);
    } else if (unknown1.Get(n)) {
      unknown1.Erase(n);
      unknown0.Set(n);
    } else if (unknown2.Get(n)) {
      unknown2.Erase(n);
      unknown1.Set(n);
      unknown0.Set(n);
    } else if (unknown3.Get(n)) {
      unknown3.Erase(n);
      unknown2.Set(n);
      unknown1.Set(n);
      unknown0.Set(n);
    }
  }

  // Increment state counts
  if (which) {
    for (auto n : neighbours) {
      if (!state0.Get(n)) {
        state0.Set(n);
      } else if (!state1.Get(n)) {
        state1.Set(n);
        state0.Erase(n);
      } else if (!state2.Get(n)) {
        state2.Set(n);
        state1.Erase(n);
        state0.Erase(n);
      }
    }
  }
}

PropagateResult LifeStableState::PropagateColumnStep(int column) {
  std::array<uint64_t, 6> nearbyStable;
  std::array<uint64_t, 6> nearbyUnknown;
  std::array<uint64_t, 6> nearbyGlanced;
  std::array<uint64_t, 6> nearbyGlancedON;

  for (int i = 0; i < 6; i++) {
    int c = (column + i - 2 + N) % N;
    nearbyStable[i] = state[c];
    nearbyUnknown[i] = unknownStable[c];
    nearbyGlanced[i] = glanced[c];
    nearbyGlancedON[i] = glancedON[c];
  }

  std::array<uint64_t, 6> oncol0;
  std::array<uint64_t, 6> oncol1;
  std::array<uint64_t, 6> unkcol0;
  std::array<uint64_t, 6> unkcol1;

  #pragma clang loop vectorize(enable)
  for (int i = 0; i < 6; i++) {
    uint64_t a = nearbyStable[i];
    uint64_t l = RotateLeft(a);
    uint64_t r = RotateRight(a);

    oncol0[i] = l ^ r ^ a;
    oncol1[i] = ((l ^ r) & a) | (l & r);
  }

  #pragma clang loop vectorize(enable)
  for (int i = 0; i < 6; i++) {
    uint64_t a = nearbyUnknown[i];
    uint64_t l = RotateLeft(a);
    uint64_t r = RotateRight(a);

    unkcol0[i] = l ^ r ^ a;
    unkcol1[i] = ((l ^ r) & a) | (l & r);
  }

  std::array<uint64_t, 6> new_off;
  std::array<uint64_t, 6> new_on;

  std::array<uint64_t, 6> signals_off {0};
  std::array<uint64_t, 6> signals_on {0};

  std::array<uint64_t, 6> signalled_off {0};
  std::array<uint64_t, 6> signalled_on {0};

  uint64_t abort = 0; // The neighbourhood is inconsistent

  #pragma clang loop vectorize(enable)
  for (int i = 1; i < 5; i++) {
    int idxU = i-1;
    int idxB = i+1;

    uint64_t on3, on2, on1, on0;
    uint64_t unk3, unk2, unk1, unk0;

    {
      uint64_t u_on1 = oncol1[idxU];
      uint64_t u_on0 = oncol0[idxU];
      uint64_t c_on1 = oncol1[i];
      uint64_t c_on0 = oncol0[i];
      uint64_t l_on1 = oncol1[idxB];
      uint64_t l_on0 = oncol0[idxB];

      uint64_t uc0, uc1, uc2, uc_carry0;
      HalfAdd(uc0, uc_carry0, u_on0, c_on0);
      FullAdd(uc1, uc2, u_on1, c_on1, uc_carry0);

      uint64_t on_carry1, on_carry0;
      HalfAdd(on0, on_carry0, uc0, l_on0);
      FullAdd(on1, on_carry1, uc1, l_on1, on_carry0);
      HalfAdd(on2, on3, uc2, on_carry1);
      on2 |= on3;
      on1 |= on3;
      on0 |= on3;

      uint64_t u_unk1 = unkcol1[idxU];
      uint64_t u_unk0 = unkcol0[idxU];
      uint64_t c_unk1 = unkcol1[i];
      uint64_t c_unk0 = unkcol0[i];
      uint64_t l_unk1 = unkcol1[idxB];
      uint64_t l_unk0 = unkcol0[idxB];

      uint64_t ucunk0, ucunk1, ucunk2, ucunk_carry0;
      HalfAdd(ucunk0, ucunk_carry0, u_unk0, c_unk0);
      FullAdd(ucunk1, ucunk2, u_unk1, c_unk1, ucunk_carry0);

      uint64_t unk_carry1, unk_carry0;
      HalfAdd(unk0, unk_carry0, ucunk0, l_unk0);
      FullAdd(unk1, unk_carry1, ucunk1, l_unk1, unk_carry0);
      HalfAdd(unk2, unk3, ucunk2, unk_carry1);
      unk1 |= unk2 | unk3;
      unk0 |= unk2 | unk3;
    }

    uint64_t stateon = nearbyStable[i];
    uint64_t stateunk   = nearbyUnknown[i];
    uint64_t gl        = nearbyGlanced[i];
    uint64_t dr        = nearbyGlancedON[i];

    uint64_t set_off = 0; // Set an UNKNOWN cell to OFF
    uint64_t set_on = 0;

    uint64_t signal_off = 0; // Set an UNKNOWN cell to OFF
    uint64_t signal_on = 0;

// Begin Autogenerated
set_off |= on2 ;
set_off |= (~on1) & ((~unk1) | ((~on0) & (~unk0)));
set_on |= (~on2) & on1 & on0 & (~unk1) ;
abort |= stateon & on2 & (on1 | on0) ;
abort |= stateon & (~on1) & on0 & (~unk1) ;
abort |= on1 & (~unk1) & (~unk0) & (((~stateon) & (~on2) & on0) | (stateon & (~on0))) ;
signal_off |= (~stateunk) & (~stateon) & (~on2) & on1 & (~on0) & (~unk1) & unk0 ;
signal_off |= stateon & (~on1) & (((~on0) & unk1) | ((~unk1) & unk0));
signal_on |= (~stateunk) & (~stateon) & (~on2) & on1 & on0 & (~unk1) ;
signal_on |= stateon & on1 & (~on0) & (~unk1) ;
signal_on |= stateon & (~on1) & on0 & (~unk0) ;
// End Autogenerated

   // A glanced cell with an ON neighbour
   signal_off |= gl & (~on2) & (~on1) & on0;
   // A glanced cell with too many neighbours
   abort |= gl & (on2 | on1);
   // A glanced cell that is ON
   abort |= gl & stateon;

   // A glancedON cell with 2 ON/UNK neighbours
   signal_on |= dr & (~unk3) & (~unk2) & (~on2) & (~on1) & (((~unk1) & unk0 & on0) | (unk1 & (~unk0) & (~on0)));
   // A glancedON cell with too few neighbours
   abort |= dr & (~unk3) & (~unk2) & (~unk1) & (~on2) & (~on1) & (((~unk0) & (~on0)) | (unk0 & (~on0)) | ((~unk0) & on0));
   // A glancedON cell that is ON
   abort |= dr & stateon;

   new_off[i] = set_off & stateunk;
   new_on[i]  = set_on  & stateunk;

   signals_off[i] = signal_off & (unk0 | unk1);
   signals_on[i]  = signal_on  & (unk0 | unk1);
  }

  if(abort != 0)
    return {false, false, false};

  #pragma clang loop vectorize(enable)
  for (int i = 1; i < 5; i++) {
   uint64_t smear_off = RotateLeft(signals_off[i]) | signals_off[i] | RotateRight(signals_off[i]);
   signalled_off[i-1] |= smear_off;
   signalled_off[i]   |= smear_off;
   signalled_off[i+1] |= smear_off;

   uint64_t smear_on  = RotateLeft(signals_on[i])  | signals_on[i]  | RotateRight(signals_on[i]);
   signalled_on[i-1] |= smear_on;
   signalled_on[i]   |= smear_on;
   signalled_on[i+1] |= smear_on;
  }

  uint64_t signalled_overlaps = 0;
  #pragma clang loop vectorize(enable)
  for (int i = 0; i < 6; i++) {
    signalled_overlaps |= nearbyUnknown[i] & signalled_off[i] & signalled_on[i];
  }
  if(signalled_overlaps != 0)
    return {false, false, false};

  #pragma clang loop vectorize(enable)
  for (int i = 1; i < 5; i++) {
    int orig = (column + i - 2 + N) % N;
    state[orig]  |= new_on[i];
    unknownStable[orig] &= ~new_off[i];
    unknownStable[orig] &= ~new_on[i];
  }

  #pragma clang loop vectorize(enable)
  for (int i = 0; i < 6; i++) {
    int orig = (column + i - 2 + N) % N;
    state[orig]  |= signalled_on[i] & nearbyUnknown[i];
    unknownStable[orig] &= ~signalled_on[i];
    unknownStable[orig] &= ~signalled_off[i];
  }

  uint64_t unknownChanges = 0;
  uint64_t edgeChanges = 0;
  #pragma clang loop vectorize(enable)
  for (int i = 0; i < 6; i++) {
    int orig = (column + i - 2 + N) % N;
    unknownChanges |= unknownStable[orig] ^ nearbyUnknown[i];
    if(i == 0 || i == 1 || i == 4 || i == 5)
      edgeChanges |= unknownStable[orig] ^ nearbyUnknown[i];
  }

  return { true, unknownChanges != 0, edgeChanges != 0 };
}

void LifeStableState::UpdateZOIColumn(int column) {
  std::array<uint64_t, 4> temp {0};
  for (int i = 0; i < 4; i++) {
    int c = (column + i - 1 + N) % N;
    uint64_t col = state[c];
    temp[i] = col | RotateLeft(col) | RotateRight(col);
  }

  {
    int i = 0;
    int c = (column + i - 1 + N) % N;
    stateZOI[c] |= temp[i] | temp[i+1];
  }
  for (int i = 1; i < 3; i++) {
    int c = (column + i - 1 + N) % N;
    stateZOI[c] |= temp[i-1] | temp[i] | temp[i+1];
  }
  {
    int i = 3;
    int c = (column + i - 1 + N) % N;
    stateZOI[c] |= temp [i-1] | temp[i];
  }
}

PropagateResult LifeStableState::PropagateColumn(int column) {
  bool done = false;
  bool changed = false;
  bool edgesChanged = false;
  while (!done) {
    auto result = PropagateColumnStep(column);
    if (!result.consistent)
      return {false, false, false};
    if (result.changed)
      changed = true;
    if (result.edgesChanged)
      edgesChanged = true;
    done = !result.changed;
  }
  UpdateZOIColumn(column);
  return {true, changed, edgesChanged};
}

PropagateResult LifeStableState::PropagateStableStep() {
  LifeState startUnknownStable = unknownStable;

  LifeState dummy(false);
  CountNeighbourhood(state, dummy, state2, state1, state0);
  CountNeighbourhood(unknownStable, unknown3, unknown2, unknown1, unknown0);

  LifeState new_off(false), new_on(false), new_signal_off(false), new_signal_on(false);

  uint64_t has_set_off = 0;
  uint64_t has_set_on = 0;
  uint64_t has_signal_off = 0;
  uint64_t has_signal_on = 0;
  uint64_t has_abort = 0;

  for (int i = 0; i < N; i++) {
    uint64_t on2 = state2[i];
    uint64_t on1 = state1[i];
    uint64_t on0 = state0[i];

    uint64_t unk3 = unknown3[i];
    uint64_t unk2 = unknown2[i];
    uint64_t unk1 = unknown1[i];
    uint64_t unk0 = unknown0[i];

    unk1 |= unk2 | unk3;
    unk0 |= unk2 | unk3;

    uint64_t stateon = state[i];
    uint64_t stateunk   = unknownStable[i];
    uint64_t gl     = glanced[i];
    uint64_t dr     = glancedON[i];

    // These are the 5 output bits that are calculated for each cell
    uint64_t set_off = 0; // Set an UNKNOWN cell to OFF
    uint64_t set_on = 0;
    uint64_t signal_off = 0; // Set all UNKNOWN neighbours of the cell to OFF
    uint64_t signal_on = 0;
    uint64_t abort = 0; // The neighbourhood is inconsistent

// Begin Autogenerated
set_off |= on2 ;
set_off |= (~on1) & ((~unk1) | ((~on0) & (~unk0)));
set_on |= (~on2) & on1 & on0 & (~unk1) ;
abort |= stateon & on2 & (on1 | on0) ;
abort |= stateon & (~on1) & on0 & (~unk1) ;
abort |= on1 & (~unk1) & (~unk0) & (((~stateon) & (~on2) & on0) | (stateon & (~on0))) ;
signal_off |= (~stateunk) & (~stateon) & (~on2) & on1 & (~on0) & (~unk1) & unk0 ;
signal_off |= stateon & (~on1) & (((~on0) & unk1) | ((~unk1) & unk0));
signal_on |= (~stateunk) & (~stateon) & (~on2) & on1 & on0 & (~unk1) ;
signal_on |= stateon & on1 & (~on0) & (~unk1) ;
signal_on |= stateon & (~on1) & on0 & (~unk0) ;
// End Autogenerated

   // A glanced cell with an ON neighbour
   signal_off |= gl & (~on2) & (~on1) & on0;
   // A glanced cell with too many neighbours
   abort |= gl & (on2 | on1);
   // A glanced cell that is ON
   abort |= gl & stateon;

   // A glancedON cell with 2 ON/UNK neighbours
   signal_on |= dr & (~unk3) & (~unk2) & (~on2) & (~on1) & (((~unk1) & unk0 & on0) | (unk1 & (~unk0) & (~on0)));
   // A glancedON cell with too few neighbours
   abort |= dr & (~unk3) & (~unk2) & (~unk1) & (~on2) & (~on1) & (((~unk0) & (~on0)) | (unk0 & (~on0)) | ((~unk0) & on0));
   // A glancedON cell that is ON
   abort |= dr & stateon;

   signal_off &= unk0 | unk1;
   signal_on  &= unk0 | unk1;

   new_off[i] = set_off & stateunk;
   new_on[i] = set_on & stateunk;
   new_signal_off[i] = signal_off;
   new_signal_on[i] = signal_on;

   has_set_off |= set_off;
   has_set_on |= set_on;
   has_signal_off |= signal_off;
   has_signal_on |= signal_on;
   has_abort |= abort;
  }

  if(has_abort != 0)
    return {false, false, false};

  if (has_set_on != 0) {
    state |= new_on;
    unknownStable &= ~new_on;
  }

  if (has_set_off != 0) {
    unknownStable &= ~new_off;
  }

  LifeState off_zoi(false);
  LifeState on_zoi(false);
  if (has_signal_off != 0) {
    off_zoi = new_signal_off.ZOI();
    unknownStable &= ~off_zoi;
  }

  if (has_signal_on != 0) {
    on_zoi = new_signal_on.ZOI();
    state |= on_zoi & unknownStable;
    unknownStable &= ~on_zoi;
  }

  if (has_signal_on != 0 && has_signal_off != 0) {
    if(!(on_zoi & off_zoi & unknownStable).IsEmpty()) {
      has_abort = 1;
    }
  }

  bool changes = unknownStable != startUnknownStable;

  return {has_abort == 0, changes, changes};
}

PropagateResult LifeStableState::PropagateStable() {
  bool done = false;
  bool changed = false;
  while (!done) {
    auto result = PropagateStableStep();
    if (!result.consistent)
      return {false, false, false};
    if (result.changed)
      changed = true;
    done = !result.changed;
  }

  stateZOI = state.ZOI();
  return {true, changed, changed};
}

std::pair<int, int> LifeStableState::UnknownNeighbour(std::pair<int, int> cell) const {
  return unknownStable.FindSetNeighbour(cell);
}

PropagateResult LifeStableState::TestUnknowns(const LifeState &cells) {
  // Try all the nearby changes to see if any are forced
  LifeState remainingCells = cells;
  bool change = false;
  while (!remainingCells.IsEmpty()) {
    auto cell = remainingCells.FirstOn();
    remainingCells.Erase(cell);

    // Try on

    LifeStableState onSearch = *this;
    onSearch.state.SetCell(cell, true);
    onSearch.unknownStable.Erase(cell);
    auto onResult = onSearch.PropagateColumn(cell.first);

    // Try off
    LifeStableState offSearch = *this;
    offSearch.state.SetCell(cell, false);
    offSearch.unknownStable.Erase(cell);
    auto offResult = offSearch.PropagateColumn(cell.first);

    if(!onResult.consistent && !offResult.consistent) {
      return {false, false, false};
    }

    if(onResult.consistent && !offResult.consistent) {
      *this = onSearch;
      change = true;
    }

    if (!onResult.consistent && offResult.consistent) {
      *this = offSearch;
      change = true;
    }

    if (onResult.consistent && offResult.consistent && onResult.changed && offResult.changed) {
      // Copy over common cells
      LifeState agreement = unknownStable & ~onSearch.unknownStable & ~offSearch.unknownStable & ~(onSearch.state ^ offSearch.state);
      if (!agreement.IsEmpty()) {
        state |= agreement & onSearch.state;
        unknownStable &= ~agreement;
        change = true;
      }
    }

    remainingCells &= unknownStable;
  }

  if (change)
    return {PropagateStable().consistent, true, true};
  else
    return {true, false, false};
}

PropagateResult LifeStableState::TestUnknownNeighbourhood(std::pair<int, int> center) {
  LifeState remainingCells = LifeState::CellZOI(center) & unknownStable;
  bool change = false;
  while (!remainingCells.IsEmpty()) {
    auto cell = remainingCells.FirstOn();
    remainingCells.Erase(cell);

    // Try on

    LifeStableState onSearch = *this;
    onSearch.state.SetCell(cell, true);
    onSearch.unknownStable.Erase(cell);
    auto onResult = onSearch.PropagateColumn(cell.first);
    bool onChanged = onResult.changed;
    if (onResult.consistent) {
      onResult = onSearch.TestUnknownNeighbourhood(center);
      onChanged = onChanged || onResult.changed;
    }

    // Try off
    LifeStableState offSearch = *this;
    offSearch.state.SetCell(cell, false);
    offSearch.unknownStable.Erase(cell);
    auto offResult = offSearch.PropagateColumn(cell.first);
    bool offChanged = offResult.changed;
    if (offResult.consistent) {
      offResult = offSearch.TestUnknownNeighbourhood(center);
      offChanged = offChanged || offResult.changed;
    }

    if(!onResult.consistent && !offResult.consistent) {
      return {false, false, false};
    }

    if(onResult.consistent && !offResult.consistent) {
      *this = onSearch;
      change = true;
    }

    if (!onResult.consistent && offResult.consistent) {
      *this = offSearch;
      change = true;
    }

    if (onResult.consistent && offResult.consistent && onChanged && offChanged) {
      // Copy over common cells
      LifeState agreement = unknownStable & ~onSearch.unknownStable & ~offSearch.unknownStable & ~(onSearch.state ^ offSearch.state);
      if (!agreement.IsEmpty()) {
        state |= agreement & onSearch.state;
        unknownStable &= ~agreement;
        change = true;
      }
    }

    remainingCells &= unknownStable;
  }

  if (change)
    return {PropagateStable().consistent, true, true};
  else
    return {true, false, false};
}

PropagateResult LifeStableState::TestUnknownNeighbourhoods(const LifeState &cells) {
  LifeState remainingCells = cells;
  bool change = false;
  while (!remainingCells.IsEmpty()) {
    auto cell = remainingCells.FirstOn();
    remainingCells.Erase(cell);
    auto result = TestUnknownNeighbourhood(cell);
    if (!result.consistent)
      return {false, false, false};
    change = change || result.changed;
  }
  return {true, change, change};
}

bool LifeStableState::CompleteStableStep(std::chrono::system_clock::time_point &timeLimit, bool minimise, unsigned &maxPop, LifeState &best) {
  auto currentTime = std::chrono::system_clock::now();
  if(currentTime > timeLimit)
    return false;

  bool consistent = PropagateStable().consistent;
  if (!consistent)
    return false;

  unsigned currentPop = state.GetPop();

  if (currentPop >= maxPop) {
    return false;
  }

  auto result = TestUnknownNeighbourhoods(~unknown3 & ~unknown2 & ~(~unknown1 & ~unknown0));
  if (!result.consistent)
    return false;

  if (result.changed) {
    currentPop = state.GetPop();
    if(currentPop >= maxPop)
      return false;
  }

  LifeState next = state;
  next.Step();

  LifeState instabilities = state ^ next;
  if (instabilities.IsEmpty()) {
    // We win
    best = state;
    maxPop = state.GetPop();
    return true;
  }

  // CHEAT!
  if (!minimise && instabilities.GetPop() + currentPop >= maxPop)
    return false;

  LifeState settable = instabilities.ZOI() & unknownStable;
  // Now make a guess
  std::pair<int, int> newPlacement = {-1, -1};

  newPlacement = (settable & (~unknown3 & ~unknown2 & unknown1 & ~unknown0)).FirstOn();
  if(newPlacement.first == -1)
    newPlacement = (settable & (~unknown3 & ~unknown2 & unknown1 & unknown0)).FirstOn();
  if(newPlacement.first == -1)
    newPlacement = settable.FirstOn();
  if(newPlacement.first == -1)
    return false;

  bool onresult = false;
  bool offresult = false;

  // Try off
  {
    bool which = false;
    LifeStableState nextState = *this;
    nextState.state.SetCell(newPlacement, which);
    nextState.unknownStable.Erase(newPlacement);
    offresult = nextState.CompleteStableStep(timeLimit, minimise, maxPop, best);
  }
  if (!minimise && offresult)
    return true;

  // Then must be on
  {
    bool which = true;
    LifeStableState &nextState = *this;
    nextState.state.SetCell(newPlacement, which);
    nextState.unknownStable.Erase(newPlacement);

    if (currentPop == maxPop - 2) {
      // All remaining unknown cells must be off
      nextState.unknownStable = LifeState();
    }

    onresult = nextState.CompleteStableStep(timeLimit, minimise, maxPop, best);
  }

  return offresult || onresult;
}

LifeState LifeStableState::CompleteStable(unsigned timeout, bool minimise) {
  LifeState best;
  unsigned maxPop = std::numeric_limits<int>::max();
  LifeState searchArea = state;

  auto startTime = std::chrono::system_clock::now();
  auto timeLimit = startTime + std::chrono::seconds(timeout);

  do {
    searchArea = searchArea.ZOI();
    LifeStableState copy = *this;
    copy.unknownStable &= searchArea;
    copy.CompleteStableStep(timeLimit, minimise, maxPop, best);

    auto currentTime = std::chrono::system_clock::now();
    if (best.GetPop() > 0 || currentTime > timeLimit)
      break;
  } while(!(unknownStable & ~searchArea).IsEmpty());
  return best;
}

LifeState LifeStableState::Vulnerable() const {
  return unknownStable
    & (
       (~unknown3 & ~unknown2 & unknown1 & ~unknown0)
       // LifeState()
//     | (~state2 &  state1 &  state0 & ~unknown3 & ~unknown2 &  unknown1 & ~unknown0) // 3 ON, 1 UNK
//     | (~state2 &  state1 &  state0 & (unknown3 | unknown2 | unknown1)) // 3 ON, any UNK
//     | (~state2 & ~state1 & ~state0 & ~unknown3 & ~unknown2 &  unknown1 &  unknown0) // 0 ON, 2 UNK 21.5
//     | (~state2 & ~state1 &  state0 & ~unknown3 & ~unknown2 &  unknown1 &  unknown0) // 1 ON, 2 UNK 20.7
//     | (~state2 &  state1 & ~state0 & ~unknown3 & ~unknown2 &  unknown1 & ~unknown0) // 2 ON, 1 UNK 20.6
       );
}
