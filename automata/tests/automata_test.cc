#include <vector>

#include "automata/grid.h"
#include "automata/grid_object.h"
#include "automata/organism.h"
#include "automata/movement_factor.h"
#include "gtest/gtest.h"

namespace automata {
namespace testing {

// The components of automata are pretty intertwined, and it's hard to test them
// individually, so this file contains unit tests for pretty much every
// component of the automata library.

class AutomataTest : public ::testing::Test {
 protected:
  AutomataTest() : grid_(9, 9) {}

  Grid grid_;
};

TEST_F(AutomataTest, OccupantTest) {
  // Do SetOccupant() and GetOccupant() work?
  EXPECT_EQ(grid_.GetOccupant(0, 0), nullptr);

  GridObject object(&grid_, 0);
  ASSERT_TRUE(object.Initialize(0, 0));
  EXPECT_TRUE(grid_.Update());
  EXPECT_EQ(grid_.GetOccupant(0, 0), &object);

  // Clear the grid again.
  grid_.SetOccupant(0, 0, nullptr);
  ASSERT_TRUE(grid_.Update());
}

TEST_F(AutomataTest, NeighborhoodTest) {
  // Does getting the indices in a neighborhood work?
  // Set the extended neighborhood of the location in the middle of the grid to
  // be all ones.
  GridObject object(&grid_, 0);
  ASSERT_TRUE(object.Initialize(0, 0));
  for (int i = 5; i <= 7; ++i) {
    grid_.SetOccupant(i, 5, &object);
    grid_.SetOccupant(i, 7, &object);
  }
  grid_.SetOccupant(5, 6, &object);
  grid_.SetOccupant(7, 6, &object);
  ASSERT_TRUE(grid_.Update());

  ::std::vector<::std::vector<GridObject *>> neighborhood;
  EXPECT_TRUE(grid_.GetNeighborhood(6, 6, &neighborhood));
  EXPECT_EQ(1, neighborhood.size());

  // Check that everything in the neighborhood is what we expect it to be.
  for (auto new_object : neighborhood[0]) {
    EXPECT_EQ(&object, new_object);
  }
}

TEST_F(AutomataTest, OutOfBoundsTest) {
  // Does GetNeighborhoodLocations deal properly with out-of-bounds input?
  ::std::vector<int> xs, ys;
  // Giving it a starting point outside the boundaries of the grid should make
  // it fail.
  EXPECT_FALSE(grid_.GetNeighborhoodLocations(-1, -1, &xs, &ys));
  // Putting it in a corner should truncate the neighborhood.
  EXPECT_TRUE(grid_.GetNeighborhoodLocations(0, 0, &xs, &ys));
  EXPECT_EQ(3, xs.size());
  EXPECT_EQ(3, ys.size());
}

TEST_F(AutomataTest, MotionTest) {
  // Does DoMovement make a reasonable choice given the array of probabilities?
  // Make a probabilities array with only one possible choice.
  double probabilities[8];
  probabilities[0] = 1;
  for (int i = 1; i < 8; ++i) {
    probabilities[i] = 0;
  }

  // Use GetNeighborhoodLocations to generate xs and ys vectors.
  ::std::vector<int> xs, ys;
  grid_.GetNeighborhoodLocations(1, 1, &xs, &ys);

  int new_x, new_y;
  grid_.DoMovement(probabilities, xs, ys, &new_x, &new_y);
  EXPECT_EQ(xs[0], new_x);
  EXPECT_EQ(ys[0], new_y);
}

TEST_F(AutomataTest, MotionFactorsTest) {
  // Do movement factors influence probabilities the way we would expect?
  ::std::vector<MovementFactor> factors;
  double probabilities[8];
  ::std::vector<int> xs, ys;
  grid_.GetNeighborhoodLocations(1, 1, &xs, &ys);

  // No factors should lead to equal probability for every location.
  grid_.CalculateProbabilities(factors, xs, ys, probabilities);
  for (int i = 1; i < 8; ++i) {
    EXPECT_EQ(probabilities[0], probabilities[i]);
  }

  // A factor with a strength of zero should have the same effect.
  MovementFactor factor(0, 0, 0, -1);
  factors.push_back(factor);
  grid_.CalculateProbabilities(factors, xs, ys, probabilities);
  for (int i = 1; i < 8; ++i) {
    EXPECT_EQ(probabilities[0], probabilities[i]);
  }

  // An attractive factor in the neighborhood should lead to a high probability
  // for its location.
  factors[0].SetStrength(100);
  grid_.CalculateProbabilities(factors, xs, ys, probabilities);
  for (int i = 1; i < 8; ++i) {
    EXPECT_GT(probabilities[0], probabilities[i]);
  }

  // Two attractive factors in opposite corners of the neighborhood should
  // create two "poles" of attraction.
  factor.SetX(2);
  factor.SetY(2);
  factor.SetStrength(100);
  factors.push_back(factor);
  grid_.CalculateProbabilities(factors, xs, ys, probabilities);
  // The two poles.
  EXPECT_EQ(probabilities[5], probabilities[0]);
  for (int i = 1; i < 8; ++i) {
    if (i != 5) {
      EXPECT_GT(probabilities[0], probabilities[i]);
    }
  }

  // A repulsive factor in the neighborhood should do the opposite.
  factors.pop_back();
  factors[0].SetStrength(-100);
  grid_.CalculateProbabilities(factors, xs, ys, probabilities);
  for (int i = 1; i < 8; ++i) {
    EXPECT_LT(probabilities[0], probabilities[i]);
  }

  // An attractive factor just outside the neighborhood should work similarly to
  // one inside the neighborhood.
  factors[0].SetX(3);
  factors[0].SetY(1);
  factors[0].SetStrength(100);
  grid_.CalculateProbabilities(factors, xs, ys, probabilities);
  for (int i = 1; i < 7; ++i) {
    EXPECT_GT(probabilities[7], probabilities[i]);
  }

  // If we blacklist factors, they should get removed.
  grid_.SetBlacklisted(2, 1, true);
  auto blacklist_xs = xs;
  auto blacklist_ys = ys;
  grid_.RemoveUnusable(&blacklist_xs, &blacklist_ys);
  EXPECT_EQ(7, blacklist_xs.size());
  EXPECT_EQ(7, blacklist_ys.size());

  // This same attractive factor should stop working if we set its visibility
  // low enough.
  auto invisible_factors = factors;
  invisible_factors[0].SetVisibility(1);
  grid_.RemoveInvisible(1, 1, &invisible_factors, -1);
  EXPECT_TRUE(invisible_factors.empty());

  // We should also get this same result if we set the organism's vision low
  // enough.
  grid_.RemoveInvisible(1, 1, &factors, 1);
  EXPECT_TRUE(factors.empty());
}

TEST_F(AutomataTest, UpdateAndConflictTest) {
  // Does the grid handle conflicts and updating correctly?
  GridObject object1(&grid_, 0);
  GridObject object2(&grid_, 1);
  ASSERT_TRUE(object1.Initialize(0, 0));
  ASSERT_TRUE(object2.Initialize(1, 1));

  // There should be no objects here yet.
  EXPECT_EQ(nullptr, grid_.GetOccupant(0, 0));
  EXPECT_EQ(nullptr, grid_.GetOccupant(1, 1));

  EXPECT_TRUE(grid_.Update());

  // Now there should be objects there.
  EXPECT_EQ(&object1, grid_.GetOccupant(0, 0));
  EXPECT_EQ(&object2, grid_.GetOccupant(1, 1));

  // Make a conflict.
  EXPECT_TRUE(object1.SetPosition(2, 2));
  EXPECT_FALSE(object2.SetPosition(2, 2));

  // Updating should not work now.
  EXPECT_FALSE(grid_.Update());

  // That didn't work, so there still shouldn't be anyone there.
  EXPECT_EQ(nullptr, grid_.GetOccupant(2, 2));
  EXPECT_EQ(nullptr, grid_.GetOccupant(2, 2));

  // We can easily resolve the conflict, though.
  EXPECT_TRUE(object2.SetPosition(0, 0));
  EXPECT_TRUE(grid_.Update());

  // Test that we have objects where we think we do.
  EXPECT_EQ(&object1, grid_.GetOccupant(2, 2));
  EXPECT_EQ(&object2, grid_.GetOccupant(0, 0));
}

// Do GetPosition and GetBakedPosition work as planned?
TEST_F(AutomataTest, PositioningTest) {
  GridObject object1(&grid_, 0);
  GridObject object2(&grid_, 1);
  ASSERT_TRUE(object1.Initialize(2, 2));
  ASSERT_TRUE(object2.Initialize(0, 0));

  // Before we update, our baked positions should be at -1.
  int baked_x, baked_y;
  object1.GetBakedPosition(&baked_x, &baked_y);
  EXPECT_EQ(-1, baked_x);
  EXPECT_EQ(-1, baked_y);

  object2.GetBakedPosition(&baked_x, &baked_y);
  EXPECT_EQ(-1, baked_x);
  EXPECT_EQ(-1, baked_y);

  ASSERT_TRUE(grid_.Update());

  // Move an object.
  EXPECT_TRUE(object1.SetPosition(0, 1));

  // Test that GetPosition and GetBakedPosition tell us the right things.
  int x, y;
  object1.get_position(&x, &y);
  EXPECT_EQ(0, x);
  EXPECT_EQ(1, y);

  object1.GetBakedPosition(&baked_x, &baked_y);
  EXPECT_EQ(2, baked_x);
  EXPECT_EQ(2, baked_y);

  object2.get_position(&x, &y);
  EXPECT_EQ(0, x);
  EXPECT_EQ(0, y);

  object2.GetBakedPosition(&baked_x, &baked_y);
  EXPECT_EQ(0, baked_x);
  EXPECT_EQ(0, baked_y);
}

// Does checking for and resolving conflicts work as expected?
TEST_F(AutomataTest, ConflictResolutionTest) {
  Organism object1(&grid_, 0);
  Organism object2(&grid_, 1);
  ASSERT_TRUE(object1.Initialize(0, 0));
  ASSERT_TRUE(object2.Initialize(1, 1));

  // Update the grid to bake our current organisms.
  ASSERT_TRUE(grid_.Update());

  // We should have no conflicts.
  ::std::vector<GridObject *> conflicts1;
  ::std::vector<GridObject *> conflicts2;
  grid_.GetConflicted(&conflicts1, &conflicts2);
  EXPECT_TRUE(conflicts1.empty());
  EXPECT_TRUE(conflicts2.empty());

  // Make a conflict.
  EXPECT_TRUE(object2.SetPosition(2, 2));
  EXPECT_FALSE(object1.SetPosition(2, 2));

  // Now it should show up.
  EXPECT_EQ(&object1, grid_.GetConflict(2, 2));
  grid_.GetConflicted(&conflicts1, &conflicts2);
  EXPECT_EQ(1, conflicts1.size());
  EXPECT_EQ(1, conflicts2.size());
  EXPECT_TRUE(conflicts1[0] == &object1 || conflicts1[0] == &object2);
  EXPECT_TRUE(conflicts1[0] == &object2 || conflicts2[0] == &object2);

  // We can use the default conflict handler to resolve this conflict.
  EXPECT_TRUE(object1.DefaultConflictHandler());

  // The conflict should be resolved.
  EXPECT_EQ(nullptr, grid_.GetConflict(2, 2));
  grid_.GetConflicted(&conflicts1, &conflicts2);
  EXPECT_TRUE(conflicts1.empty());
  EXPECT_TRUE(conflicts2.empty());

  // Bake the new positions.
  EXPECT_TRUE(grid_.Update());

  // Make the same conflict again.
  EXPECT_TRUE(object2.SetPosition(1, 1));
  EXPECT_FALSE(object1.SetPosition(1, 1));
  EXPECT_EQ(&object1, grid_.GetConflict(1, 1));

  // The handler should work just as well if we run it on the pending object.
  EXPECT_TRUE(object2.DefaultConflictHandler());

  // The conflict should be resolved.
  EXPECT_EQ(nullptr, grid_.GetConflict(1, 1));
  grid_.GetConflicted(&conflicts1, &conflicts2);
  EXPECT_TRUE(conflicts1.empty());
  EXPECT_TRUE(conflicts2.empty());
}

// The mechanism for requesting that a cell stay the same to the next cycle is
// kind of intricate. Does it work as planned?
TEST_F(AutomataTest, StasisRequestTest) {
  GridObject object1(&grid_, 0);
  GridObject object2(&grid_, 1);
  ASSERT_TRUE(object1.Initialize(0, 0));
  ASSERT_TRUE(object2.Initialize(1, 1));
  EXPECT_TRUE(grid_.Update());

  // We should be able to overwrite (0, 0).
  EXPECT_TRUE(grid_.SetOccupant(0, 0, &object2));
  // Trying to write it back to the original should create a conflict.
  EXPECT_FALSE(grid_.SetOccupant(0, 0, &object1));
  // Clear the conflict and purge object2.
  EXPECT_TRUE(grid_.PurgeNew(0, 0, &object1));
  EXPECT_TRUE(grid_.PurgeNew(0, 0, &object2));

  // Now, request that (0, 0) retains the same occupant.
  EXPECT_TRUE(grid_.SetOccupant(0, 0, &object1));
  // We should get a conflict if we try to put something else there.
  EXPECT_FALSE(grid_.SetOccupant(0, 0, &object2));
  // Clear the conflict.
  EXPECT_TRUE(grid_.PurgeNew(0, 0, &object2));
  // If we update, it should indeed stay the same.
  EXPECT_TRUE(grid_.Update());
  EXPECT_EQ(&object1, grid_.GetOccupant(0, 0));

  // There's an interesting edge case where a conflict gets promoted when we
  // clear the pending slot.
  EXPECT_TRUE(grid_.SetOccupant(0, 0, &object2));
  EXPECT_FALSE(grid_.SetOccupant(0, 0, &object1));
  // Now, clear the pending slot. The conflict should move into the pending
  // slot.
  EXPECT_TRUE(grid_.PurgeNew(0, 0, &object2));
  // At this point, we should get a conflict if we try to set it to object2
  // again.
  EXPECT_FALSE(grid_.SetOccupant(0, 0, &object2));
}

}  //  testing
}  //  automata
