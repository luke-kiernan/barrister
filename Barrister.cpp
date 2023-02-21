#include <cassert>

#include "LifeAPI.h"
#include "Parsing.hpp"
#include "Bits.hpp"
#include "LifeStableState.hpp"
#include "LifeUnknownState.hpp"

const unsigned maxLookaheadGens = 10;
const unsigned maxLocalGens = 3;
const unsigned maxActive = 5;
const unsigned maxEverActive = 10;
const unsigned maxInteractionWindow = 6;
const unsigned stableTime = 2;

class SearchState {
public:

  LifeState starting;
  LifeStableState stable;
  LifeUnknownState current;
  // std::vector<LifeUnknownState> lookahead;

  LifeState pendingFocuses;
  LifeUnknownState focusGeneration;
  LifeState everActive;

  unsigned currentGen;
  bool hasInteracted;
  unsigned interactionStart;
  unsigned recoveredTime;

  // // Cells in this generation that need to be determined
  // LifeState newUnknown;
  // LifeState newGlancing;
  // Focus focus;

  SearchState();
  SearchState ( const SearchState & ) = default;
  SearchState &operator= ( const SearchState & ) = default;

  void TransferStableToCurrent();
  bool TryAdvance();
  bool TryAdvanceOne();
  std::pair<std::array<LifeUnknownState, maxLookaheadGens>, int> PopulateLookahead() const;

  std::pair<LifeState, LifeUnknownState> FindFocuses(std::array<LifeUnknownState, maxLookaheadGens> &lookahead, int lookaheadSize) const;

  bool CheckConditionsOn(LifeState &active, LifeState &everActive) const;
  bool CheckConditions(std::array<LifeUnknownState, maxLookaheadGens> &lookahead, int lookaheadSize);

  void Search();
  void SearchStep();

  void SanityCheck();
};

// std::string SearchState::LifeBellmanRLE() const {
//   LifeState state = stable | params.activePattern;
//   LifeState marked =  unknown | stable;
//   return LifeBellmanRLEFor(state, marked);
// }

SearchState::SearchState() : currentGen {0}, hasInteracted{false}, interactionStart{0}, recoveredTime{0} {
  everActive = LifeState();
  pendingFocuses = LifeState();
}

void SearchState::TransferStableToCurrent() {
  // Places that in current are unknownStable might be updated now
  LifeState updated = current.unknownStable & ~stable.unknownStable;
  current.state |= stable.state & updated;
  current.unknown &= ~updated;
  current.unknownStable &= ~updated;
}

bool SearchState::TryAdvanceOne() {
  LifeUnknownState next = current.UncertainStepMaintaining(stable);
  bool fullyKnown = (next.unknown ^ next.unknownStable).IsEmpty();

  if (!fullyKnown)
    return false;

  if (!hasInteracted) {
    LifeState steppedWithoutStable = (current.state & ~stable.state);
    steppedWithoutStable.Step();

    bool isDifferent = !(next.state ^ steppedWithoutStable).IsEmpty();

    if (isDifferent) {
      hasInteracted = true;
      interactionStart = currentGen;
    }
  }

  current = next;
  currentGen++;

  if (hasInteracted) {
    LifeState stableZOI = stable.state.ZOI();
    bool isRecovered = ((stable.state ^ current.state) & stableZOI).IsEmpty();
    if (isRecovered)
      recoveredTime++;
    else
      recoveredTime = 0;
  }

  return true;
}

bool SearchState::TryAdvance() {
  bool didAdvance;
  while (didAdvance = TryAdvanceOne(), didAdvance) {
    LifeState active = current.ActiveComparedTo(stable);
    everActive |= active;

    if (!CheckConditionsOn(active, everActive)) {
      return false;
    }

    if (hasInteracted && currentGen - interactionStart > maxInteractionWindow)
      return false;

    if (hasInteracted && recoveredTime > stableTime) {
      LifeState completed = stable.CompleteStable();

      std::cout << "Winner:" << std::endl;
      std::cout << "x = 0, y = 0, rule = LifeBellman" << std::endl;
      LifeState state = starting | stable.state;
      LifeState marked = stable.unknownStable | stable.state;
      std::cout << LifeBellmanRLEFor(state, marked) << std::endl;
      std::cout << "Completed:" << std::endl;
      std::cout << (completed | starting).RLE() << std::endl;

      return false;
    }
  }

  return true;
}

std::pair<std::array<LifeUnknownState, maxLookaheadGens>, int> SearchState::PopulateLookahead() const {
  auto lookahead = std::array<LifeUnknownState, maxLookaheadGens>();
  lookahead[0] = current;
  int i;
  for (i = 0; i < maxLookaheadGens-1; i++) {
    lookahead[i+1] = lookahead[i].UncertainStepMaintaining(stable);

    LifeState active = lookahead[i+1].ActiveComparedTo(stable);
    if(active.IsEmpty())
      return {lookahead, i+2};
  }
  return {lookahead, maxLookaheadGens};
}

std::pair<LifeState, LifeUnknownState> SearchState::FindFocuses(std::array<LifeUnknownState, maxLookaheadGens> &lookahead, int lookaheadSize) const {
  std::array<LifeState, maxLookaheadGens> allFocusable;
  for (int i = 1; i < lookaheadSize; i++) {
    LifeUnknownState &gen = lookahead[i];
    LifeUnknownState &prev = lookahead[i-1];

    LifeState becomeUnknown = (gen.unknown & ~gen.unknownStable) & ~prev.unknown;
    LifeState nearActiveUnknown = (prev.unknown & ~prev.unknownStable).ZOI();

    allFocusable[i] = becomeUnknown & ~nearActiveUnknown;
  }

  // IDEA: calculate a 'priority' area, for example cells outside
  // the permitted 'everActive' area
  LifeState rect = LifeState::SolidRect(- maxEverActiveSize, - maxEverActiveSize, 2 * maxEverActiveSize - 1, 2 * maxEverActiveSize - 1);
  LifeState priority = ~rect.Convolve(everActive);

  for (int i = std::min((unsigned)maxLocalGens, (unsigned)lookaheadSize)-1; i >= 1; i--) {
    // LifeUnknownState &gen = lookahead[i];
    // LifeUnknownState &prev = lookahead[i-1];

    // LifeState becomeUnknown = (gen.unknown & ~gen.unknownStable) & ~prev.unknown;
    // LifeState nearActiveUnknown = (prev.unknown & ~prev.unknownStable).ZOI();
    // LifeState focusable = becomeUnknown & ~nearActiveUnknown;
    LifeState focusable = allFocusable[i];

    focusable &= priority;

    if (!focusable.IsEmpty()) {
      return {focusable, lookahead[i-1]};
    }
  }

  // IDEA: look for focusable cells where all the unknown neighbours
  // are unknownStable, that will stop us from wasting time on an
  // expanding unknown region
  for (int i = std::min((unsigned)maxLocalGens, (unsigned)lookaheadSize)-1; i >= 1; i--) {
    // LifeUnknownState &gen = lookahead[i];
    // LifeUnknownState &prev = lookahead[i-1];

    // LifeState becomeUnknown = (gen.unknown & ~gen.unknownStable) & ~prev.unknown;
    // LifeState nearActiveUnknown = (prev.unknown & ~prev.unknownStable).ZOI();
    // LifeState focusable = becomeUnknown & ~nearActiveUnknown;
    LifeState focusable = allFocusable[i];

    LifeState oneStableUnknownNeighbour  =  stable.unknown0 & ~stable.unknown1 & ~stable.unknown2 & ~stable.unknown3;
    LifeState twoStableUnknownNeighbours = ~stable.unknown0 &  stable.unknown1 & ~stable.unknown2 & ~stable.unknown3;
    focusable &= stable.stateZOI & (oneStableUnknownNeighbour | twoStableUnknownNeighbours);

    // LifeState oneStableUnknownNeighbour  =  stable.unknown0 & ~stable.unknown1 & ~stable.unknown2 & ~stable.unknown3;
    // focusable &= stable.stateZOI & oneStableUnknownNeighbour;

    // focusable &= stable.stateZOI & oneStableUnknownNeighbour;

    if (!focusable.IsEmpty()) {
      return {focusable, lookahead[i-1]};
    }
  }


  // // Try anything in the stable ZOI
  // for (int i = 1; i < lookaheadSize; i++) {
  //   // LifeUnknownState &gen = lookahead[i];
  //   // LifeUnknownState &prev = lookahead[i-1];

  //   // LifeState becomeUnknown = (gen.unknown & ~gen.unknownStable) & ~prev.unknown;
  //   // LifeState nearActiveUnknown = (prev.unknown & ~prev.unknownStable).ZOI();
  //   // LifeState focusable = becomeUnknown & ~nearActiveUnknown;
  //   LifeState focusable = allFocusable[i];

  //   focusable &= stable.stateZOI;

  //   if (!focusable.IsEmpty()) {
  //     return {focusable, lookahead[i-1]};
  //   }
  // }

  // Try anything at all
  for (int i = 1; i < lookaheadSize; i++) {
    LifeUnknownState &gen = lookahead[i];
    LifeUnknownState &prev = lookahead[i-1];
    LifeState becomeUnknown = (gen.unknown & ~gen.unknownStable) & ~(prev.unknown & ~prev.unknownStable);
    LifeState focusable = becomeUnknown;

    if (!focusable.IsEmpty()) {
      return {focusable, lookahead[i-1]};
    }
  }

  // // Try anything at all
  // for (int i = 1; i < lookaheadSize; i++) {
  //   LifeState focusable = allFocusable[i];

  //   if (!focusable.IsEmpty()) {
  //     return {focusable, lookahead[i-1]};
  //   }
  // }

  // This shouldn't be reached
  return {LifeState(), LifeUnknownState()};
}

bool SearchState::CheckConditionsOn(LifeState &active, LifeState &everActive) const {
  if (active.GetPop() > maxActive)
    return false;

  auto wh = active.WidthHeight();
  // std::cout << wh.first << ", " << wh.second << std::endl;
  int maxDim = std::max(wh.first, wh.second);
  if (maxDim > maxActiveSize)
    return false;

  if (everActive.GetPop() > maxEverActive)
    return false;

  wh = everActive.WidthHeight();
  maxDim = std::max(wh.first, wh.second);
  if (maxDim > maxEverActiveSize)
    return false;

  return true;
}

bool SearchState::CheckConditions(std::array<LifeUnknownState, maxLookaheadGens> &lookahead, int lookaheadSize) {
  for (int i = 0; i < lookaheadSize; i++) {
    LifeUnknownState &gen = lookahead[i];

    LifeState active = gen.ActiveComparedTo(stable);

    everActive |= active;
    bool genResult = CheckConditionsOn(active, everActive);

    if (!genResult)
      return false;
  }

  return true;
}

void SearchState::SanityCheck() {
  assert((current.unknownStable & ~current.unknown).IsEmpty());
  assert((stable.state & stable.unknownStable).IsEmpty());
  assert((current.unknownStable & ~stable.unknownStable).IsEmpty());
}

void SearchState::Search() {
  current.state = starting | stable.state;
  current.unknown = stable.unknownStable;
  current.unknownStable = stable.unknownStable;

  SearchStep();
}

void SearchState::SearchStep() {
  if(pendingFocuses.IsEmpty()) {
    bool consistent = stable.PropagateStable();
    if (!consistent) {
      //    std::cout << "not consistent" << std::endl;
      return;
    }

    TransferStableToCurrent();

    if (!TryAdvance()) {
      //std::cout << "advance failed" << std::endl;
      return;
    }

    if(!hasInteracted && currentGen > maxStartTime)
      return;

    // std::cout << "Stable" << std::endl;
    // LifeState state = starting | stable.state;
    // LifeState marked = stable.unknownStable | stable.state;
    // std::cout << "x = 0, y = 0, rule = LifeBellman" << std::endl;
    // std::cout << LifeBellmanRLEFor(state, marked) << std::endl;
    // std::cout << "Current" << std::endl;
    // std::cout << "x = 0, y = 0, rule = LifeBellman" << std::endl;
    // std::cout << LifeBellmanRLEFor(current.state, current.unknown) << std::endl;

    auto [lookahead, lookaheadSize] = PopulateLookahead();

    if (!CheckConditions(lookahead, lookaheadSize)) {
      //std::cout << "conditions failed" << std::endl;
      return;
    }

    std::tie(pendingFocuses, focusGeneration) = FindFocuses(lookahead, lookaheadSize); // C++ wtf

    SanityCheck();
  }

  auto focus = pendingFocuses.FirstOn();
  if (focus == std::pair(-1, -1)) {
    // Shouldn't be possible
    std::cout << "no focus" << std::endl;
    exit(1);
  }

  bool focusIsDetermined = focusGeneration.KnownNext(focus);

  auto cell = stable.UnknownNeighbour(focus);
  if(focusIsDetermined || cell == std::pair(-1, -1)) {
    pendingFocuses.Erase(focus);
    SearchStep();
    return;
  }

  {
    bool which = true;
    SearchState nextState = *this;

    nextState.stable.state.SetCellUnsafe(cell, which);
    nextState.stable.unknownStable.Erase(cell);

    nextState.focusGeneration.state.SetCellUnsafe(cell, which);
    nextState.focusGeneration.unknown.Erase(cell);
    nextState.focusGeneration.unknownStable.Erase(cell);

    bool consistent = nextState.stable.SimplePropagateColumnStep(cell.first);
    if(consistent)
      nextState.SearchStep();
  }
  {
    bool which = false;
    SearchState &nextState = *this;

    nextState.stable.state.SetCellUnsafe(cell, which);
    nextState.stable.unknownStable.Erase(cell);

    nextState.focusGeneration.state.SetCellUnsafe(cell, which);
    nextState.focusGeneration.unknown.Erase(cell);
    nextState.focusGeneration.unknownStable.Erase(cell);

    bool consistent = nextState.stable.SimplePropagateColumnStep(cell.first);
    if(consistent)
      nextState.SearchStep();
  }
}

int main(int argc, char *argv[]) {

  std::string rle = "x = 29, y = 30, rule = LifeHistory\n\
8.21B$8.21B$8.21B$8.21B$8.21B$8.21B$8.21B$8.21B$8.21B$8.21B$8.21B$8.\n\
21B$3.2A3.21B$2.A2.A2.21B$3.3A2.21B$8.21B$8.21B$29B$29B$29B$29B$29B$\n\
29B$29B$29B$29B$29B$29B$29B$29B!\n";

  LifeState on;
  LifeState marked;
  ParseTristateWHeader(rle, on, marked);

  SearchState search;
  search.starting = on;
  search.stable.state = LifeState();
  search.stable.unknownStable = marked;

  search.Search();
}
