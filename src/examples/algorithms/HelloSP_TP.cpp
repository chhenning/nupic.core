/* ---------------------------------------------------------------------
 * Numenta Platform for Intelligent Computing (NuPIC)
 * Copyright (C) 2013-2015, Numenta, Inc.  Unless you have an agreement
 * with Numenta, Inc., for a separate license for this software code, the
 * following terms and conditions apply:
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Affero Public License for more details.
 *
 * You should have received a copy of the GNU Affero Public License
 * along with this program.  If not, see http://www.gnu.org/licenses.
 *
 * http://numenta.org/licenses/
 * ---------------------------------------------------------------------
 */

#include <algorithm> // std::generate
#include <cmath>     // pow
#include <ctime>     // std::time
#include <iostream>
#include <vector>

#include "nupic/algorithms/Cells4.hpp"
#include "nupic/algorithms/SpatialPooler.hpp"
#include "nupic/os/Timer.hpp"
#include "nupic/utils/Random.hpp"

using namespace std;
using namespace nupic;
using nupic::algorithms::Cells4::Cells4;
using nupic::algorithms::spatial_pooler::SpatialPooler;

int main(int argc, const char *argv[]) {
  const UInt DIM = 2048; // number of columns in SP, TP
  const UInt DIM_INPUT = 10000;
  const UInt TP_CELLS_PER_COL = 10; // cells per column in TP
  const UInt EPOCHS = (UInt)pow(10, 4); // number of iterations (calls to SP/TP compute() )

  vector<UInt> inputDim = {DIM_INPUT};
  vector<UInt> colDim = {DIM};

  // generate random input
  vector<UInt> input(DIM_INPUT);
  vector<UInt> outSP(DIM); // active array, output of SP/TP
  const int _CELLS = DIM * TP_CELLS_PER_COL;
  vector<UInt> outTP(_CELLS);
  Real rIn[DIM] = {}; // input for TP (must be Reals)
  Real rOut[_CELLS] = {};
  Random rnd;

  // initialize SP, TP
  SpatialPooler sp(inputDim, colDim);
  Cells4 tp(DIM, TP_CELLS_PER_COL, 12, 8, 15, 5, .5f, .8f, 1.0f, .1f, .1f, 0.0f,
            false, 42, true, false);

  // Start a stopwatch timer
  printf("starting:  %d iterations.", EPOCHS);
  Timer stopwatch(true);

  // run
  for (UInt e = 0; e < EPOCHS; e++) {
    generate(input.begin(), input.end(), [&] () { return rnd.getUInt32(2); });
    fill(outSP.begin(), outSP.end(), 0);
    sp.compute(input.data(), true, outSP.data());
    sp.stripUnlearnedColumns(outSP.data());

    for (UInt i = 0; i < DIM; i++) {
      rIn[i] = (Real)(outSP[i]);
    }

    tp.compute(rIn, rOut, true, true);

    for (UInt i = 0; i < _CELLS; i++) {
      outTP[i] = (UInt)rOut[i];
    }

    // print
    if (e == EPOCHS - 1) {
      cout << "Epoch = " << e << endl;
      cout << "SP=" << outSP << endl;
      cout << "TP=" << outTP << endl;
    }
  }

  stopwatch.stop();
  cout << "Total elapsed time = " << stopwatch.getElapsed() << " seconds"
       << endl;

  return 0;
}
