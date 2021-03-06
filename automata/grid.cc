#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>  // TEMP
#include <stdlib.h>
#include <time.h>

#include <algorithm>

#include "automata/grid.h"
// We need the complete version of GridObject in this file, but we must use the
// forward-declared incomplete version in the header because including it there
// would cause a circular dependency issue.
#include "automata/grid_object.h"

namespace automata {

Grid::Grid(int x_size, int y_size)
    : x_size_(x_size), y_size_(y_size), grid_(new Cell[x_size * y_size]) {
  srand(time(NULL));

  assert(grid_ && "Failed to allocate grid array!\n");

  // Set everything to a default initialization.
  for (int i = 0; i < x_size * y_size; ++i) {
    grid_[i].Object = nullptr;
    grid_[i].NewObject = nullptr;
    grid_[i].ConflictedObject = nullptr;
    grid_[i].Blacklisted = false;
    grid_[i].RequestStasis = false;
  }
}

Grid::~Grid() {
  // The reason we're calling this here is because Python's garbage collector
  // sometimes likes to destroy a grid before it destroys things that depend
  // on that grid, leading to odd segfaults when it goes to destroy the
  // dependents, and those dependents try to remove themselves from the
  // destroyed grid in their destructors.
  for (int i = 0; i < x_size_ * y_size_; ++i) {
    if (grid_[i].Object) {
      // Technically, RemoveFromGrid() can return false, but there's not much we
      // can do about it if it does.
      grid_[i].Object->RemoveFromGrid();
    }
    if (grid_[i].NewObject) {
      grid_[i].NewObject->RemoveFromGrid();
    }
    if (grid_[i].ConflictedObject) {
      grid_[i].ConflictedObject->RemoveFromGrid();
    }
  }

  delete[] grid_;
}

bool Grid::SetOccupant(int x, int y, GridObject *occupant) {
  Cell *cell = &grid_[x * x_size_ + y];
  if (cell->Blacklisted) {
    if (!occupant || occupant == cell->NewObject) {
      // We wouldn't do anything anyway in these cases, so this is not a
      // failure.
      return true;
    }
    // We can't really put something on a blacklisted cell.
    return false;
  }

  if (!cell->NewObject ||
      (cell->NewObject == cell->Object && !cell->RequestStasis)) {
    // No occupants.
    assert(!cell->ConflictedObject && "Found conflict on vacant cell.");
    cell->NewObject = occupant;

    if (occupant == cell->Object) {
      // This is an explicit request to keep this cell the same for the next
      // cycle.
      cell->RequestStasis = true;
    }
  } else {
    // We have a conflict.
    if (!occupant || occupant == cell->NewObject) {
      // Setting NewObject to the same thing over again is not a failure, but
      // doesn't do anything. Same with setting it to nullptr if it's already
      // occupied.
      return true;
    }

    cell->ConflictedObject = occupant;
    return false;
  }

  return true;
}

bool Grid::PurgeNew(int x, int y, const GridObject *object) {
  Cell *cell = &grid_[x * x_size_ + y];
  if (object == cell->NewObject) {
    bool stasis = false;
    if (cell->ConflictedObject) {
      // Our conflict isn't a conflict anymore.
      if (cell->ConflictedObject == cell->Object) {
        // Edge case: We are explicitly requesting that this slot not change.
        cell->RequestStasis = stasis = true;
      }

      cell->NewObject = cell->ConflictedObject;
      cell->ConflictedObject = nullptr;
    } else {
      cell->NewObject = cell->Object;
    }

    if (!stasis) {
      // Things can move here again.
      cell->RequestStasis = false;
    }
  } else if (object == cell->ConflictedObject) {
    // Remove conflicted object.
    cell->ConflictedObject = nullptr;
  } else {
    // Could not find object.
    return false;
  }

  return true;
}

GridObject *Grid::GetPending(int x, int y) {
  const Cell *cell = &grid_[x * x_size_ + y];
  if (cell->NewObject == cell->Object && !cell->RequestStasis) {
    // Technically, there is nothing pending insertion here.
    return nullptr;
  }

  return cell->NewObject;
}

bool Grid::GetNeighborhoodLocations(int x, int y, ::std::list<int> *xs,
                                    ::std::list<int> *ys,
                                    int levels /* = 1*/) {
  if (x < 0 || y < 0 || x >= x_size_ || y >= y_size_) {
    // The starting point isn't within the bounds of the grid.
    return false;
  }

  int x_size = 3;
  int y_size = 3;
  for (int level = 1; level <= levels; ++level) {
    const int end_x = x + x_size / 2;
    const int end_y = y + y_size / 2;
    const int start_x = x - x_size / 2;
    const int start_y = y - y_size / 2;

    ::std::vector<int> level_indices;

    // Get the top row and the bottom row.
    for (int i = start_x; i <= end_x; ++i) {
      if (i >= 0 && i < x_size_) {
        if (start_y >= 0) {
          // Point is in-bounds.
          xs->push_back(i);
          ys->push_back(start_y);
        }
        if (end_y < y_size_) {
          xs->push_back(i);
          ys->push_back(end_y);
        }
      }
    }
    // Get the left and right columns, taking into account the corners, which
    // were already accounted for.
    for (int i = start_y + 1; i <= end_y - 1; ++i) {
      if (i >= 0 && i < y_size_) {
        if (start_x >= 0) {
          xs->push_back(start_x);
          ys->push_back(i);
        }
        if (end_x < x_size_) {
          xs->push_back(end_x);
          ys->push_back(i);
        }
      }
    }

    // Compute the new dimensions of the neighborhood.
    x_size += 2;
    y_size += 2;
  }

  return true;
}

bool Grid::GetNeighborhood(
    int x, int y, ::std::vector< ::std::vector<GridObject *> > *objects,
    int levels /*= 1*/, bool get_new /*= false*/) {
  objects->clear();

  ::std::list<int> xs, ys;
  if (!GetNeighborhoodLocations(x, y, &xs, &ys, levels)) {
    return false;
  }

  // We know how many locations are in each level, so we can divide our results
  // by level.
  uint32_t in_level = 8;
  uint32_t current_i = 0;
  auto x_itr = xs.begin();
  auto y_itr = ys.begin();
  while (x_itr != xs.end()) {
    ::std::vector<GridObject *> level_objects;
    for (; current_i < in_level && x_itr != xs.end();
         ++x_itr, ++y_itr, ++current_i) {
      GridObject *occupant;
      if (get_new) {
        occupant = GetPending(*x_itr, *y_itr);
      } else {
        occupant = GetOccupant(*x_itr, *y_itr);
      }
      if (occupant) {
        level_objects.push_back(occupant);
      }
    }

    // Add the contents of this level to our main output vector.
    objects->push_back(level_objects);

    in_level += 4;
  }

  return true;
}

bool Grid::MoveObject(int x, int y,
                      const ::std::list<MovementFactor> &factors, int *new_x,
                      int *new_y, int levels /* = 1*/, int vision /* = -1*/) {
  ::std::list<MovementFactor> visible_factors = factors;
  RemoveInvisible(x, y, &visible_factors, vision);

  ::std::list<int> xs, ys;
  if (!GetNeighborhoodLocations(x, y, &xs, &ys, levels)) {
    return false;
  }
  // We want it to have the possibility of staying in the same place also.
  xs.push_back(x);
  ys.push_back(y);
  // Remove blacklisted and conflicted locations from consideration.
  RemoveUnusable(&xs, &ys);

  double probabilities[8];
  CalculateProbabilities(visible_factors, xs, ys, probabilities);

  DoMovement(probabilities, xs, ys, new_x, new_y);

  if (x == *new_x && y == *new_y) {
    printf("Staying in the same place.\n");
  }

  return true;
}

void Grid::CalculateProbabilities(::std::list<MovementFactor> &factors,
                                  const ::std::list<int> &xs,
                                  const ::std::list<int> &ys,
                                  double *probabilities) {
  int total_strength = 0;
  for (auto &factor : factors) {
    // There is an edge case where all our factors could have a strength of
    // zero.
    total_strength += factor.GetStrength();
  }
  // Having the factor list empty is valid. It means that there are no
  // factors, and that therefore, there should be an equal probability for every
  // neighborhood location.
  if (factors.empty() || !total_strength) {
    for (uint32_t i = 0; i < xs.size(); ++i) {
      probabilities[i] = 1.0 / xs.size();
    }
    printf("Using equal probabilities.\n");
    return;
  }

  for (uint32_t i = 0; i < xs.size(); ++i) {
    probabilities[i] = 0;
  }

  // Calculate how far each factor is from each location and use it to change
  // the probabilities.
  for (auto &factor : factors) {
  	auto x_itr = xs.begin();
  	auto y_itr = ys.begin();
    for (uint32_t i = 0; i < xs.size(); ++i, ++x_itr, ++y_itr) {
      const double radius = factor.GetDistance(*x_itr, *y_itr);

      if (radius != 0) {
        probabilities[i] += (1.0 / pow(radius, 5)) * factor.GetStrength();
      } else {
        // If our factor is in the same location that we are.
        probabilities[i] += 10 * factor.GetStrength();
      }
    }
  }

  // Scale probabilities to between 0 and 1.
  double min = 0;
  for (uint32_t i = 0; i < xs.size(); ++i) {
    // First, divide to find the average.
    probabilities[i] /= factors.size();
    // Find the min.
    min = ::std::min(min, probabilities[i]);
  }
  double total = 0;
  for (uint32_t i = 0; i < xs.size(); ++i) {
    // Shift everything to make it positive and calculate total.
    probabilities[i] = (probabilities[i] - min);
    total += probabilities[i];
  }
  for (uint32_t i = 0; i < xs.size(); ++i) {
    // Do the scaling.
    probabilities[i] /= total;
    printf("Probabilities: %f\n", probabilities[i]);
  }
}

void Grid::DoMovement(const double *probabilities, const ::std::list<int> &xs,
                      const ::std::list<int> &ys, int *new_x, int *new_y) {
  // Get a random float that's somewhere between 0 and 1.
  const double random =
      static_cast<double>(rand()) / static_cast<double>(RAND_MAX);

  // Count up until we're above it.
  double running_total = 0;
  auto x_itr = xs.begin();
  auto y_itr = ys.begin();
  for (uint32_t i = 0; i < xs.size(); ++i, ++x_itr, ++y_itr) {
    running_total += probabilities[i];
    if (running_total >= random) {
      *new_x = *x_itr;
      *new_y = *y_itr;
      return;
    }
  }
  // Floating point weirdness could get us here...
  *new_x = *(--x_itr);
  *new_y = *(--y_itr);
}

void Grid::RemoveInvisible(int x, int y, ::std::list<MovementFactor> *factors,
                           int vision) {
  auto itr = factors->begin();
  for (; itr != factors->end(); ++itr) {
    const double radius = itr->GetDistance(x, y);

    printf("Radius: %f\n", radius);
    if (((*itr).GetVisibility() > 0 &&
         radius > (*itr).GetVisibility()) ||
        (vision > 0 && radius > vision)) {
      // Decrement here so that it still points to something valid afterwards.
      auto temp_itr = itr;
      --itr;
      factors->erase(temp_itr);
    }
  }
}

void Grid::RemoveUnusable(::std::list<int> *xs, ::std::list<int> *ys) {
	auto x_itr = xs->begin();
	auto y_itr = ys->begin();
  for (; x_itr != xs->end(); ++x_itr, ++y_itr) {
    const Cell *cell = &grid_[*x_itr * x_size_ + *y_itr];
    if (cell->Blacklisted || cell->ConflictedObject) {
      // This cell is blacklisted or unusable. Remove it from consideration.
      auto temp_x = x_itr;
      auto temp_y = y_itr;
      // Decrement here so that it still points to something valid afterwards.
      --x_itr;
      --y_itr;
      xs->erase(temp_x);
      ys->erase(temp_y);
    }
  }
}

bool Grid::Update() {
  for (int i = 0; i < x_size_ * y_size_; ++i) {
    if (grid_[i].ConflictedObject) {
      // We can't update if we still have unresolved conflicts.
      return false;
    }

    grid_[i].Object = grid_[i].NewObject;
    // Setting them both to be the same by default allows nullptr to be a valid
    // thing to swap in.
    grid_[i].Blacklisted = false;
    grid_[i].RequestStasis = false;
  }

  return true;
}

void Grid::GetConflicted(::std::vector<GridObject *> *objects1,
                         ::std::vector<GridObject *> *objects2) {
  objects1->clear();
  objects2->clear();

  for (int i = 0; i < x_size_ * y_size_; ++i) {
    if (grid_[i].ConflictedObject) {
      objects1->push_back(grid_[i].NewObject);
      objects2->push_back(grid_[i].ConflictedObject);
    }
  }
}

}  //  automata
