#include "mesh_fix.h"

// ============================================================
// Voxel / SDF / Marching Cubes functions
// Ported from mesh-fix-lib.js (_voxelizeToSDF, _marchingCubes,
// _laplacianSmooth, _projectToOriginalSurface,
// _repairViaVoxelReconstruction, _selectVoxelResolution)
// ============================================================

// --- Marching Cubes edge table (256 entries) ---
static const int MC_EDGE_TABLE[256] = {
    0x0,0x109,0x203,0x30a,0x406,0x50f,0x605,0x70c,0x80c,0x905,0xa0f,0xb06,0xc0a,0xd03,0xe09,0xf00,
    0x190,0x99,0x393,0x29a,0x596,0x49f,0x795,0x69c,0x99c,0x895,0xb9f,0xa96,0xd9a,0xc93,0xf99,0xe90,
    0x230,0x339,0x33,0x13a,0x636,0x73f,0x435,0x53c,0xa3c,0xb35,0x83f,0x936,0xe3a,0xf33,0xc39,0xd30,
    0x3a0,0x2a9,0x1a3,0xaa,0x7a6,0x6af,0x5a5,0x4ac,0xbac,0xaa5,0x9af,0x8a6,0xfaa,0xea3,0xda9,0xca0,
    0x460,0x569,0x663,0x76a,0x66,0x16f,0x265,0x36c,0xc6c,0xd65,0xe6f,0xf66,0x86a,0x963,0xa69,0xb60,
    0x5f0,0x4f9,0x7f3,0x6fa,0x1f6,0xff,0x3f5,0x2fc,0xdfc,0xcf5,0xfff,0xef6,0x9fa,0x8f3,0xbf9,0xaf0,
    0x650,0x759,0x453,0x55a,0x256,0x35f,0x55,0x15c,0xe5c,0xf55,0xc5f,0xd56,0xa5a,0xb53,0x859,0x950,
    0x7c0,0x6c9,0x5c3,0x4ca,0x3c6,0x2cf,0x1c5,0xcc,0xfcc,0xec5,0xdcf,0xcc6,0xbca,0xac3,0x9c9,0x8c0,
    0x8c0,0x9c9,0xac3,0xbca,0xcc6,0xdcf,0xec5,0xfcc,0xcc,0x1c5,0x2cf,0x3c6,0x4ca,0x5c3,0x6c9,0x7c0,
    0x950,0x859,0xb53,0xa5a,0xd56,0xc5f,0xf55,0xe5c,0x15c,0x55,0x35f,0x256,0x55a,0x453,0x759,0x650,
    0xaf0,0xbf9,0x8f3,0x9fa,0xef6,0xfff,0xcf5,0xdfc,0x2fc,0x3f5,0xff,0x1f6,0x6fa,0x7f3,0x4f9,0x5f0,
    0xb60,0xa69,0x963,0x86a,0xf66,0xe6f,0xd65,0xc6c,0x36c,0x265,0x16f,0x66,0x76a,0x663,0x569,0x460,
    0xca0,0xda9,0xea3,0xfaa,0x8a6,0x9af,0xaa5,0xbac,0x4ac,0x5a5,0x6af,0x7a6,0xaa,0x1a3,0x2a9,0x3a0,
    0xd30,0xc39,0xf33,0xe3a,0x936,0x83f,0xb35,0xa3c,0x53c,0x435,0x73f,0x636,0x13a,0x33,0x339,0x230,
    0xe90,0xf99,0xc93,0xd9a,0xa96,0xb9f,0x895,0x99c,0x69c,0x795,0x49f,0x596,0x29a,0x393,0x99,0x190,
    0xf00,0xe09,0xd03,0xc0a,0xb06,0xa0f,0x905,0x80c,0x70c,0x605,0x50f,0x406,0x30a,0x203,0x109,0x0
};

// --- Marching Cubes triangle table (256 x 16, -1 terminated) ---
static const int MC_TRI_TABLE[256][16] = {
    {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,8,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,1,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,8,3,9,8,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,2,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,8,3,1,2,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {9,2,10,0,2,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {2,8,3,2,10,8,10,9,8,-1,-1,-1,-1,-1,-1,-1},
    {3,11,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,11,2,8,11,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,9,0,2,3,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,11,2,1,9,11,9,8,11,-1,-1,-1,-1,-1,-1,-1},
    {3,10,1,11,10,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,10,1,0,8,10,8,11,10,-1,-1,-1,-1,-1,-1,-1},
    {3,9,0,3,11,9,11,10,9,-1,-1,-1,-1,-1,-1,-1},
    {9,8,10,10,8,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,7,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,3,0,7,3,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,1,9,8,4,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,1,9,4,7,1,7,3,1,-1,-1,-1,-1,-1,-1,-1},
    {1,2,10,8,4,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {3,4,7,3,0,4,1,2,10,-1,-1,-1,-1,-1,-1,-1},
    {9,2,10,9,0,2,8,4,7,-1,-1,-1,-1,-1,-1,-1},
    {2,10,9,2,9,7,2,7,3,7,9,4,-1,-1,-1,-1},
    {8,4,7,3,11,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {11,4,7,11,2,4,2,0,4,-1,-1,-1,-1,-1,-1,-1},
    {9,0,1,8,4,7,2,3,11,-1,-1,-1,-1,-1,-1,-1},
    {4,7,11,9,4,11,9,11,2,9,2,1,-1,-1,-1,-1},
    {3,10,1,3,11,10,7,8,4,-1,-1,-1,-1,-1,-1,-1},
    {1,11,10,1,4,11,1,0,4,7,11,4,-1,-1,-1,-1},
    {4,7,8,9,0,11,9,11,10,11,0,3,-1,-1,-1,-1},
    {4,7,11,4,11,9,9,11,10,-1,-1,-1,-1,-1,-1,-1},
    {9,5,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {9,5,4,0,8,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,5,4,1,5,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {8,5,4,8,3,5,3,1,5,-1,-1,-1,-1,-1,-1,-1},
    {1,2,10,9,5,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {3,0,8,1,2,10,4,9,5,-1,-1,-1,-1,-1,-1,-1},
    {5,2,10,5,4,2,4,0,2,-1,-1,-1,-1,-1,-1,-1},
    {2,10,5,3,2,5,3,5,4,3,4,8,-1,-1,-1,-1},
    {9,5,4,2,3,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,11,2,0,8,11,4,9,5,-1,-1,-1,-1,-1,-1,-1},
    {0,5,4,0,1,5,2,3,11,-1,-1,-1,-1,-1,-1,-1},
    {2,1,5,2,5,8,2,8,11,4,8,5,-1,-1,-1,-1},
    {10,3,11,10,1,3,9,5,4,-1,-1,-1,-1,-1,-1,-1},
    {4,9,5,0,8,1,8,10,1,8,11,10,-1,-1,-1,-1},
    {5,4,0,5,0,11,5,11,10,11,0,3,-1,-1,-1,-1},
    {5,4,8,5,8,10,10,8,11,-1,-1,-1,-1,-1,-1,-1},
    {9,7,8,5,7,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {9,3,0,9,5,3,5,7,3,-1,-1,-1,-1,-1,-1,-1},
    {0,7,8,0,1,7,1,5,7,-1,-1,-1,-1,-1,-1,-1},
    {1,5,3,3,5,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {9,7,8,9,5,7,10,1,2,-1,-1,-1,-1,-1,-1,-1},
    {10,1,2,9,5,0,5,3,0,5,7,3,-1,-1,-1,-1},
    {8,0,2,8,2,5,8,5,7,10,5,2,-1,-1,-1,-1},
    {2,10,5,2,5,3,3,5,7,-1,-1,-1,-1,-1,-1,-1},
    {7,9,5,7,8,9,3,11,2,-1,-1,-1,-1,-1,-1,-1},
    {9,5,7,9,7,2,9,2,0,2,7,11,-1,-1,-1,-1},
    {2,3,11,0,1,8,1,7,8,1,5,7,-1,-1,-1,-1},
    {11,2,1,11,1,7,7,1,5,-1,-1,-1,-1,-1,-1,-1},
    {9,5,8,8,5,7,10,1,3,10,3,11,-1,-1,-1,-1},
    {5,7,0,5,0,9,7,11,0,1,0,10,11,10,0,-1},
    {11,10,0,11,0,3,10,5,0,8,0,7,5,7,0,-1},
    {11,10,5,7,11,5,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {10,6,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,8,3,5,10,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {9,0,1,5,10,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,8,3,1,9,8,5,10,6,-1,-1,-1,-1,-1,-1,-1},
    {1,6,5,2,6,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,6,5,1,2,6,3,0,8,-1,-1,-1,-1,-1,-1,-1},
    {9,6,5,9,0,6,0,2,6,-1,-1,-1,-1,-1,-1,-1},
    {5,9,8,5,8,2,5,2,6,3,2,8,-1,-1,-1,-1},
    {2,3,11,10,6,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {11,0,8,11,2,0,10,6,5,-1,-1,-1,-1,-1,-1,-1},
    {0,1,9,2,3,11,5,10,6,-1,-1,-1,-1,-1,-1,-1},
    {5,10,6,1,9,2,9,11,2,9,8,11,-1,-1,-1,-1},
    {6,3,11,6,5,3,5,1,3,-1,-1,-1,-1,-1,-1,-1},
    {0,8,11,0,11,5,0,5,1,5,11,6,-1,-1,-1,-1},
    {3,11,6,0,3,6,0,6,5,0,5,9,-1,-1,-1,-1},
    {6,5,9,6,9,11,11,9,8,-1,-1,-1,-1,-1,-1,-1},
    {5,10,6,4,7,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,3,0,4,7,3,6,5,10,-1,-1,-1,-1,-1,-1,-1},
    {1,9,0,5,10,6,8,4,7,-1,-1,-1,-1,-1,-1,-1},
    {10,6,5,1,9,7,1,7,3,7,9,4,-1,-1,-1,-1},
    {6,1,2,6,5,1,4,7,8,-1,-1,-1,-1,-1,-1,-1},
    {1,2,5,5,2,6,3,0,4,3,4,7,-1,-1,-1,-1},
    {8,4,7,9,0,5,0,6,5,0,2,6,-1,-1,-1,-1},
    {7,3,9,7,9,4,3,2,9,5,9,6,2,6,9,-1},
    {3,11,2,7,8,4,10,6,5,-1,-1,-1,-1,-1,-1,-1},
    {5,10,6,4,7,2,4,2,0,2,7,11,-1,-1,-1,-1},
    {0,1,9,4,7,8,2,3,11,5,10,6,-1,-1,-1,-1},
    {9,2,1,9,11,2,9,4,11,7,11,4,5,10,6,-1},
    {8,4,7,3,11,5,3,5,1,5,11,6,-1,-1,-1,-1},
    {5,1,11,5,11,6,1,0,11,7,11,4,0,4,11,-1},
    {0,5,9,0,6,5,0,3,6,11,6,3,8,4,7,-1},
    {6,5,9,6,9,11,4,7,9,7,11,9,-1,-1,-1,-1},
    {10,4,9,6,4,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,10,6,4,9,10,0,8,3,-1,-1,-1,-1,-1,-1,-1},
    {10,0,1,10,6,0,6,4,0,-1,-1,-1,-1,-1,-1,-1},
    {8,3,1,8,1,6,8,6,4,6,1,10,-1,-1,-1,-1},
    {1,4,9,1,2,4,2,6,4,-1,-1,-1,-1,-1,-1,-1},
    {3,0,8,1,2,9,2,4,9,2,6,4,-1,-1,-1,-1},
    {0,2,4,4,2,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {8,3,2,8,2,4,4,2,6,-1,-1,-1,-1,-1,-1,-1},
    {10,4,9,10,6,4,11,2,3,-1,-1,-1,-1,-1,-1,-1},
    {0,8,2,2,8,11,4,9,10,4,10,6,-1,-1,-1,-1},
    {3,11,2,0,1,6,0,6,4,6,1,10,-1,-1,-1,-1},
    {6,4,1,6,1,10,4,8,1,2,1,11,8,11,1,-1},
    {9,6,4,9,3,6,9,1,3,11,6,3,-1,-1,-1,-1},
    {8,11,1,8,1,0,11,6,1,9,1,4,6,4,1,-1},
    {3,11,6,3,6,0,0,6,4,-1,-1,-1,-1,-1,-1,-1},
    {6,4,8,11,6,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {7,10,6,7,8,10,8,9,10,-1,-1,-1,-1,-1,-1,-1},
    {0,7,3,0,10,7,0,9,10,6,7,10,-1,-1,-1,-1},
    {10,6,7,1,10,7,1,7,8,1,8,0,-1,-1,-1,-1},
    {10,6,7,10,7,1,1,7,3,-1,-1,-1,-1,-1,-1,-1},
    {1,2,6,1,6,8,1,8,9,8,6,7,-1,-1,-1,-1},
    {2,6,9,2,9,1,6,7,9,0,9,3,7,3,9,-1},
    {7,8,0,7,0,6,6,0,2,-1,-1,-1,-1,-1,-1,-1},
    {7,3,2,6,7,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {2,3,11,10,6,8,10,8,9,8,6,7,-1,-1,-1,-1},
    {2,0,7,2,7,11,0,9,7,6,7,10,9,10,7,-1},
    {1,8,0,1,7,8,1,10,7,6,7,10,2,3,11,-1},
    {11,2,1,11,1,7,10,6,1,6,7,1,-1,-1,-1,-1},
    {8,9,6,8,6,7,9,1,6,11,6,3,1,3,6,-1},
    {0,9,1,11,6,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {7,8,0,7,0,6,3,11,0,11,6,0,-1,-1,-1,-1},
    {7,11,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {7,6,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {3,0,8,11,7,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,1,9,11,7,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {8,1,9,8,3,1,11,7,6,-1,-1,-1,-1,-1,-1,-1},
    {10,1,2,6,11,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,2,10,3,0,8,6,11,7,-1,-1,-1,-1,-1,-1,-1},
    {2,9,0,2,10,9,6,11,7,-1,-1,-1,-1,-1,-1,-1},
    {6,11,7,2,10,3,10,8,3,10,9,8,-1,-1,-1,-1},
    {7,2,3,6,2,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {7,0,8,7,6,0,6,2,0,-1,-1,-1,-1,-1,-1,-1},
    {2,7,6,2,3,7,0,1,9,-1,-1,-1,-1,-1,-1,-1},
    {1,6,2,1,8,6,1,9,8,8,7,6,-1,-1,-1,-1},
    {10,7,6,10,1,7,1,3,7,-1,-1,-1,-1,-1,-1,-1},
    {10,7,6,1,7,10,1,8,7,1,0,8,-1,-1,-1,-1},
    {0,3,7,0,7,10,0,10,9,6,10,7,-1,-1,-1,-1},
    {7,6,10,7,10,8,8,10,9,-1,-1,-1,-1,-1,-1,-1},
    {6,8,4,11,8,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {3,6,11,3,0,6,0,4,6,-1,-1,-1,-1,-1,-1,-1},
    {8,6,11,8,4,6,9,0,1,-1,-1,-1,-1,-1,-1,-1},
    {9,4,6,9,6,3,9,3,1,11,3,6,-1,-1,-1,-1},
    {6,8,4,6,11,8,2,10,1,-1,-1,-1,-1,-1,-1,-1},
    {1,2,10,3,0,11,0,6,11,0,4,6,-1,-1,-1,-1},
    {4,11,8,4,6,11,0,2,9,2,10,9,-1,-1,-1,-1},
    {10,9,3,10,3,2,9,4,3,11,3,6,4,6,3,-1},
    {8,2,3,8,4,2,4,6,2,-1,-1,-1,-1,-1,-1,-1},
    {0,4,2,4,6,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,9,0,2,3,4,2,4,6,4,3,8,-1,-1,-1,-1},
    {1,9,4,1,4,2,2,4,6,-1,-1,-1,-1,-1,-1,-1},
    {8,1,3,8,6,1,8,4,6,6,10,1,-1,-1,-1,-1},
    {10,1,0,10,0,6,6,0,4,-1,-1,-1,-1,-1,-1,-1},
    {4,6,3,4,3,8,6,10,3,0,3,9,10,9,3,-1},
    {10,9,4,6,10,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,9,5,7,6,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,8,3,4,9,5,11,7,6,-1,-1,-1,-1,-1,-1,-1},
    {5,0,1,5,4,0,7,6,11,-1,-1,-1,-1,-1,-1,-1},
    {11,7,6,8,3,4,3,5,4,3,1,5,-1,-1,-1,-1},
    {9,5,4,10,1,2,7,6,11,-1,-1,-1,-1,-1,-1,-1},
    {6,11,7,1,2,10,0,8,3,4,9,5,-1,-1,-1,-1},
    {7,6,11,5,4,10,4,2,10,4,0,2,-1,-1,-1,-1},
    {3,4,8,3,5,4,3,2,5,10,5,2,11,7,6,-1},
    {7,2,3,7,6,2,5,4,9,-1,-1,-1,-1,-1,-1,-1},
    {9,5,4,0,8,6,0,6,2,6,8,7,-1,-1,-1,-1},
    {3,6,2,3,7,6,1,5,0,5,4,0,-1,-1,-1,-1},
    {6,2,8,6,8,7,2,1,8,4,8,5,1,5,8,-1},
    {9,5,4,10,1,6,1,7,6,1,3,7,-1,-1,-1,-1},
    {1,6,10,1,7,6,1,0,7,8,7,0,9,5,4,-1},
    {4,0,10,4,10,5,0,3,10,6,10,7,3,7,10,-1},
    {7,6,10,7,10,8,5,4,10,4,8,10,-1,-1,-1,-1},
    {6,9,5,6,11,9,11,8,9,-1,-1,-1,-1,-1,-1,-1},
    {3,6,11,0,6,3,0,5,6,0,9,5,-1,-1,-1,-1},
    {0,11,8,0,5,11,0,1,5,5,6,11,-1,-1,-1,-1},
    {6,11,3,6,3,5,5,3,1,-1,-1,-1,-1,-1,-1,-1},
    {1,2,10,9,5,11,9,11,8,11,5,6,-1,-1,-1,-1},
    {0,11,3,0,6,11,0,9,6,5,6,9,1,2,10,-1},
    {11,8,5,11,5,6,8,0,5,10,5,2,0,2,5,-1},
    {6,11,3,6,3,5,2,10,3,10,5,3,-1,-1,-1,-1},
    {5,8,9,5,2,8,5,6,2,3,8,2,-1,-1,-1,-1},
    {9,5,6,9,6,0,0,6,2,-1,-1,-1,-1,-1,-1,-1},
    {1,5,8,1,8,0,5,6,8,3,8,2,6,2,8,-1},
    {1,5,6,2,1,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,3,6,1,6,10,3,8,6,5,6,9,8,9,6,-1},
    {10,1,0,10,0,6,9,5,0,5,6,0,-1,-1,-1,-1},
    {0,3,8,5,6,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {10,5,6,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {11,5,10,7,5,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {11,5,10,11,7,5,8,3,0,-1,-1,-1,-1,-1,-1,-1},
    {5,11,7,5,10,11,1,9,0,-1,-1,-1,-1,-1,-1,-1},
    {10,7,5,10,11,7,9,8,1,8,3,1,-1,-1,-1,-1},
    {11,1,2,11,7,1,7,5,1,-1,-1,-1,-1,-1,-1,-1},
    {0,8,3,1,2,7,1,7,5,7,2,11,-1,-1,-1,-1},
    {9,7,5,9,2,7,9,0,2,2,11,7,-1,-1,-1,-1},
    {7,5,2,7,2,11,5,9,2,3,2,8,9,8,2,-1},
    {2,5,10,2,3,5,3,7,5,-1,-1,-1,-1,-1,-1,-1},
    {8,2,0,8,5,2,8,7,5,10,2,5,-1,-1,-1,-1},
    {9,0,1,5,10,3,5,3,7,3,10,2,-1,-1,-1,-1},
    {9,8,2,9,2,1,8,7,2,10,2,5,7,5,2,-1},
    {1,3,5,3,7,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,8,7,0,7,1,1,7,5,-1,-1,-1,-1,-1,-1,-1},
    {9,0,3,9,3,5,5,3,7,-1,-1,-1,-1,-1,-1,-1},
    {9,8,7,5,9,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {5,8,4,5,10,8,10,11,8,-1,-1,-1,-1,-1,-1,-1},
    {5,0,4,5,11,0,5,10,11,11,3,0,-1,-1,-1,-1},
    {0,1,9,8,4,10,8,10,11,10,4,5,-1,-1,-1,-1},
    {10,11,4,10,4,5,11,3,4,9,4,1,3,1,4,-1},
    {2,5,1,2,8,5,2,11,8,4,5,8,-1,-1,-1,-1},
    {0,4,11,0,11,3,4,5,11,2,11,1,5,1,11,-1},
    {0,2,5,0,5,9,2,11,5,4,5,8,11,8,5,-1},
    {9,4,5,2,11,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {2,5,10,3,5,2,3,4,5,3,8,4,-1,-1,-1,-1},
    {5,10,2,5,2,4,4,2,0,-1,-1,-1,-1,-1,-1,-1},
    {3,10,2,3,5,10,3,8,5,4,5,8,0,1,9,-1},
    {5,10,2,5,2,4,1,9,2,9,4,2,-1,-1,-1,-1},
    {8,4,5,8,5,3,3,5,1,-1,-1,-1,-1,-1,-1,-1},
    {0,4,5,1,0,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {8,4,5,8,5,3,9,0,5,0,3,5,-1,-1,-1,-1},
    {9,4,5,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,11,7,4,9,11,9,10,11,-1,-1,-1,-1,-1,-1,-1},
    {0,8,3,4,9,7,9,11,7,9,10,11,-1,-1,-1,-1},
    {1,10,11,1,11,4,1,4,0,7,4,11,-1,-1,-1,-1},
    {3,1,4,3,4,8,1,10,4,7,4,11,10,11,4,-1},
    {4,11,7,9,11,4,9,2,11,9,1,2,-1,-1,-1,-1},
    {9,7,4,9,11,7,9,1,11,2,11,1,0,8,3,-1},
    {11,7,4,11,4,2,2,4,0,-1,-1,-1,-1,-1,-1,-1},
    {11,7,4,11,4,2,8,3,4,3,2,4,-1,-1,-1,-1},
    {2,9,10,2,7,9,2,3,7,7,4,9,-1,-1,-1,-1},
    {9,10,7,9,7,4,10,2,7,0,7,8,2,8,7,-1},
    {3,7,10,3,10,2,7,4,10,1,10,0,4,0,10,-1},
    {1,10,2,8,7,4,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,9,1,4,1,7,7,1,3,-1,-1,-1,-1,-1,-1,-1},
    {4,9,1,4,1,7,0,8,1,8,7,1,-1,-1,-1,-1},
    {4,0,3,7,4,3,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {4,8,7,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {9,10,8,10,11,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {3,0,9,3,9,11,11,9,10,-1,-1,-1,-1,-1,-1,-1},
    {0,1,10,0,10,8,8,10,11,-1,-1,-1,-1,-1,-1,-1},
    {3,1,10,11,3,10,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,2,11,1,11,9,9,11,8,-1,-1,-1,-1,-1,-1,-1},
    {3,0,9,3,9,11,1,2,9,2,11,9,-1,-1,-1,-1},
    {0,2,11,8,0,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {3,2,11,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {2,3,8,2,8,10,10,8,9,-1,-1,-1,-1,-1,-1,-1},
    {9,10,2,0,9,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {2,3,8,2,8,10,0,1,8,1,10,8,-1,-1,-1,-1},
    {1,10,2,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {1,3,8,9,1,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,9,1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {0,3,8,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1},
    {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}
};

// --- MC edge corners: edge index -> [corner0, corner1] ---
static const int MC_EDGE_CORNERS[12][2] = {
    {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}
};

// --- MC corner offsets: corner index -> [di, dj, dk] (standard ordering) ---
static const int MC_CORNER_OFFSETS[8][3] = {
    {0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}
};

// rayTriangleIntersect defined in self_intersect.cpp

// ============================================================
// selectVoxelResolution
// ============================================================
int MeshFix::selectVoxelResolution(const ScaleInfo& scale, int userResolution) const {
    if (userResolution > 0) {
        return std::max(32, std::min(512, userResolution));
    }
    double target = scale.bboxDiag / (scale.medianEdge * 0.5);
    int res = static_cast<int>(std::round(target));
    return std::max(64, std::min(256, res));
}

// ============================================================
// voxelizeToSDF
// ============================================================
SDFGrid MeshFix::voxelizeToSDF(const std::vector<Vec3>& V, const std::vector<Tri>& T,
                                int resolution, ProgressCallback progress) const {
    SDFGrid result;

    // Compute bounding box
    double minX = 1e30, minY = 1e30, minZ = 1e30;
    double maxX = -1e30, maxY = -1e30, maxZ = -1e30;
    for (size_t i = 0; i < V.size(); i++) {
        const auto& v = V[i];
        if (v[0] < minX) minX = v[0]; if (v[0] > maxX) maxX = v[0];
        if (v[1] < minY) minY = v[1]; if (v[1] > maxY) maxY = v[1];
        if (v[2] < minZ) minZ = v[2]; if (v[2] > maxZ) maxZ = v[2];
    }
    double sizeX = maxX - minX, sizeY = maxY - minY, sizeZ = maxZ - minZ;
    double maxSize = std::max({sizeX, sizeY, sizeZ});
    if (maxSize < 1e-10) {
        // Degenerate mesh - return empty grid
        return result;
    }

    double voxelSize = maxSize / (resolution - 4);
    double padding = 2.0 * voxelSize;
    Vec3 origin = {minX - padding, minY - padding, minZ - padding};

    int resX = static_cast<int>(std::ceil((sizeX + 2.0 * padding) / voxelSize));
    int resY = static_cast<int>(std::ceil((sizeY + 2.0 * padding) / voxelSize));
    int resZ = static_cast<int>(std::ceil((sizeZ + 2.0 * padding) / voxelSize));
    size_t gridSize = static_cast<size_t>(resX) * resY * resZ;
    std::vector<float> grid(gridSize, 0.0f);

    // Build BVH for nearest triangle queries
    BVH* bvh = buildBVH(V, T);
    int reportInterval = std::max(1, resZ / 10);

    // Pass 1: Compute unsigned distance
    progress("SDF construction... (distance calculation)");
    for (int k = 0; k < resZ; k++) {
        if (k % reportInterval == 0) {
            int pct = 40 * k / resZ;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "SDF construction... %d%%", pct);
            progress(buf);
        }
        for (int j = 0; j < resY; j++) {
            for (int i = 0; i < resX; i++) {
                Vec3 point = {
                    origin[0] + i * voxelSize,
                    origin[1] + j * voxelSize,
                    origin[2] + k * voxelSize
                };
                NearestResult nearest = findNearestTriangle(point, V, T, bvh);
                grid[i + j * resX + k * resX * resY] = static_cast<float>(nearest.distance);
            }
        }
    }

    // Pass 2: 3-axis ray casting with majority vote for sign determination
    progress("SDF construction... (sign determination)");
    std::vector<int8_t> signVotes(gridSize, 0);

    int resArr[3] = {resX, resY, resZ};
    double dirs[3][3] = {{1,0,0},{0,1,0},{0,0,1}};
    double jitterEps = voxelSize * 1e-4;

    for (int ax = 0; ax < 3; ax++) {
        if (ax > 0) {
            int pct = 40 + 20 * ax;
            char buf[64];
            std::snprintf(buf, sizeof(buf), "SDF construction... %d%%", pct);
            progress(buf);
        }
        Vec3 dir = {dirs[ax][0], dirs[ax][1], dirs[ax][2]};
        int pAxis = ax;
        int s1Axis = (ax + 1) % 3;
        int s2Axis = (ax + 2) % 3;
        int resPrimary = resArr[pAxis];
        int resS1 = resArr[s1Axis];
        int resS2 = resArr[s2Axis];

        for (int b = 0; b < resS2; b++) {
            for (int a = 0; a < resS1; a++) {
                // Ray origin: primary axis = origin - voxelSize, others = voxel position + jitter
                Vec3 rayOrig = {0.0, 0.0, 0.0};
                rayOrig[pAxis] = origin[pAxis] - voxelSize;
                rayOrig[s1Axis] = origin[s1Axis] + a * voxelSize + jitterEps;
                rayOrig[s2Axis] = origin[s2Axis] + b * voxelSize + jitterEps;

                // Intersect with all triangles
                std::vector<double> hits;
                for (size_t ti = 0; ti < T.size(); ti++) {
                    RayHit hit = rayTriangleIntersect(rayOrig, dir, V, T[ti][0], T[ti][1], T[ti][2]);
                    if (hit.hit && hit.t > 0) hits.push_back(hit.t);
                }
                std::sort(hits.begin(), hits.end());

                // Remove duplicates
                std::vector<double> uniqueHits;
                for (size_t h = 0; h < hits.size(); h++) {
                    if (h == 0 || hits[h] - hits[h - 1] > voxelSize * 0.01) {
                        uniqueHits.push_back(hits[h]);
                    }
                }

                // Scanline: walk along primary axis
                size_t hitIdx = 0;
                bool inside = false;
                for (int p = 0; p < resPrimary; p++) {
                    double voxelDist = origin[pAxis] + p * voxelSize - rayOrig[pAxis];
                    while (hitIdx < uniqueHits.size() && uniqueHits[hitIdx] < voxelDist) {
                        inside = !inside;
                        hitIdx++;
                    }
                    if (inside) {
                        // Compute grid index
                        int coords[3] = {0, 0, 0};
                        coords[pAxis] = p;
                        coords[s1Axis] = a;
                        coords[s2Axis] = b;
                        signVotes[coords[0] + coords[1] * resX + coords[2] * resX * resY]++;
                    }
                }
            }
        }
    }

    // Majority vote: 2+ votes = inside (negative distance)
    progress("SDF construction... 90%");
    for (size_t idx = 0; idx < gridSize; idx++) {
        if (signVotes[idx] >= 2) {
            grid[idx] = -grid[idx];
        }
    }

    result.data = std::move(grid);
    result.resX = resX;
    result.resY = resY;
    result.resZ = resZ;
    result.origin = origin;
    result.voxelSize = voxelSize;

    delete bvh;
    return result;
}

// ============================================================
// marchingCubes
// ============================================================
MeshFix::MCResult MeshFix::marchingCubes(const SDFGrid& sdf) const {
    MCResult mc;
    const auto& data = sdf.data;
    int resX = sdf.resX, resY = sdf.resY, resZ = sdf.resZ;
    const auto& origin = sdf.origin;
    double voxelSize = sdf.voxelSize;

    std::unordered_map<int64_t, int> edgeVertexCache;

    int stride = (resY + 1) * (resZ + 1);

    // Lambda: get or create vertex on an edge (with cache for manifold guarantee)
    auto getEdgeVertex = [&](int i, int j, int k, int edgeIdx) -> int {
        int c0 = MC_EDGE_CORNERS[edgeIdx][0];
        int c1 = MC_EDGE_CORNERS[edgeIdx][1];
        int gi0 = i + MC_CORNER_OFFSETS[c0][0], gj0 = j + MC_CORNER_OFFSETS[c0][1], gk0 = k + MC_CORNER_OFFSETS[c0][2];
        int gi1 = i + MC_CORNER_OFFSETS[c1][0], gj1 = j + MC_CORNER_OFFSETS[c1][1], gk1 = k + MC_CORNER_OFFSETS[c1][2];
        int mi = std::min(gi0, gi1), mj = std::min(gj0, gj1), mk = std::min(gk0, gk1);
        int axis = (gi0 != gi1) ? 0 : ((gj0 != gj1) ? 1 : 2);
        int64_t key = static_cast<int64_t>(mi * stride + mj * (resZ + 1) + mk) * 3 + axis;

        auto it = edgeVertexCache.find(key);
        if (it != edgeVertexCache.end()) return it->second;

        float v0 = data[gi0 + gj0 * resX + gk0 * resX * resY];
        float v1 = data[gi1 + gj1 * resX + gk1 * resX * resY];

        double t = 0.5;
        if (std::abs(v1 - v0) > 1e-10) {
            t = std::max(0.0, std::min(1.0, static_cast<double>(-v0) / (v1 - v0)));
        }

        int vi = static_cast<int>(mc.V.size());
        mc.V.push_back({
            origin[0] + (gi0 + t * (gi1 - gi0)) * voxelSize,
            origin[1] + (gj0 + t * (gj1 - gj0)) * voxelSize,
            origin[2] + (gk0 + t * (gk1 - gk0)) * voxelSize
        });
        edgeVertexCache[key] = vi;
        return vi;
    };

    for (int k = 0; k < resZ - 1; k++) {
        for (int j = 0; j < resY - 1; j++) {
            for (int i = 0; i < resX - 1; i++) {
                // Evaluate 8 corners
                int cubeIndex = 0;
                float vals[8];
                for (int c = 0; c < 8; c++) {
                    int ci = i + MC_CORNER_OFFSETS[c][0];
                    int cj = j + MC_CORNER_OFFSETS[c][1];
                    int ck = k + MC_CORNER_OFFSETS[c][2];
                    vals[c] = data[ci + cj * resX + ck * resX * resY];
                    if (vals[c] < 0) cubeIndex |= (1 << c);
                }

                if (cubeIndex == 0 || cubeIndex == 255) continue;
                int edges = MC_EDGE_TABLE[cubeIndex];
                if (edges == 0) continue;

                const int* row = MC_TRI_TABLE[cubeIndex];
                for (int t = 0; row[t] != -1; t += 3) {
                    int v0 = getEdgeVertex(i, j, k, row[t]);
                    int v1 = getEdgeVertex(i, j, k, row[t + 1]);
                    int v2 = getEdgeVertex(i, j, k, row[t + 2]);
                    if (v0 != v1 && v1 != v2 && v2 != v0) {
                        mc.T.push_back({v0, v1, v2});
                    }
                }
            }
        }
    }

    return mc;
}

// ============================================================
// laplacianSmooth
// ============================================================
std::vector<Vec3> MeshFix::laplacianSmooth(const std::vector<Vec3>& V, const std::vector<Tri>& T,
                                            int iterations, double lambda) const {
    int n = static_cast<int>(V.size());

    // Build adjacency graph
    std::vector<std::vector<int>> adj(n);
    for (size_t i = 0; i < T.size(); i++) {
        int a = T[i][0], b = T[i][1], c = T[i][2];
        // Add neighbor if not already present
        auto addNeighbor = [](std::vector<int>& list, int v) {
            for (size_t j = 0; j < list.size(); j++) {
                if (list[j] == v) return;
            }
            list.push_back(v);
        };
        addNeighbor(adj[a], b);
        addNeighbor(adj[a], c);
        addNeighbor(adj[b], a);
        addNeighbor(adj[b], c);
        addNeighbor(adj[c], a);
        addNeighbor(adj[c], b);
    }

    std::vector<Vec3> verts = V;
    for (int iter = 0; iter < iterations; iter++) {
        std::vector<Vec3> newV(n);
        for (int i = 0; i < n; i++) {
            const auto& neighbors = adj[i];
            if (neighbors.empty()) {
                newV[i] = {verts[i][0], verts[i][1], verts[i][2]};
                continue;
            }
            double ax = 0.0, ay = 0.0, az = 0.0;
            for (size_t j = 0; j < neighbors.size(); j++) {
                ax += verts[neighbors[j]][0];
                ay += verts[neighbors[j]][1];
                az += verts[neighbors[j]][2];
            }
            double invN = 1.0 / neighbors.size();
            ax *= invN; ay *= invN; az *= invN;
            newV[i] = {
                verts[i][0] + lambda * (ax - verts[i][0]),
                verts[i][1] + lambda * (ay - verts[i][1]),
                verts[i][2] + lambda * (az - verts[i][2])
            };
        }
        verts = std::move(newV);
    }
    return verts;
}

// ============================================================
// projectToOriginalSurface
// ============================================================
std::vector<Vec3> MeshFix::projectToOriginalSurface(const std::vector<Vec3>& newV,
                                                     const std::vector<Vec3>& origV,
                                                     const std::vector<Tri>& origT,
                                                     const BVH* bvh, double maxDist) const {
    std::vector<Vec3> projected(newV.size());
    for (size_t i = 0; i < newV.size(); i++) {
        NearestResult nearest = findNearestTriangle(newV[i], origV, origT, bvh);
        if (nearest.distance <= maxDist && nearest.triIdx >= 0) {
            projected[i] = nearest.closest;
        } else {
            projected[i] = {newV[i][0], newV[i][1], newV[i][2]};
        }
    }
    return projected;
}

// ============================================================
// repairViaVoxelReconstruction
// ============================================================
MeshFix::VoxelResult MeshFix::repairViaVoxelReconstruction(const std::vector<Vec3>& V,
                                                            const std::vector<Tri>& T,
                                                            ProgressCallback progress,
                                                            const RepairOptions& opts) const {
    ScaleInfo scale = analyzeScale(V, T);
    int resolution = selectVoxelResolution(scale, opts.voxelResolution);

    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "Voxel reconstruction (resolution: %d)...", resolution);
        progress(buf);
    }

    // Build SDF
    SDFGrid sdf = voxelizeToSDF(V, T, resolution, progress);
    if (sdf.data.empty()) {
        return {std::vector<Vec3>(V), std::vector<Tri>(T), 0};
    }

    // Thin geometry check: if any axis < 8 voxels, skip reconstruction
    if (sdf.resX < 8 || sdf.resY < 8 || sdf.resZ < 8) {
        progress("Deep Repair skipped: geometry too thin");
        return {std::vector<Vec3>(V), std::vector<Tri>(T), 0};
    }

    // Marching Cubes
    progress("Running Marching Cubes...");
    double savedVoxelSize = sdf.voxelSize;
    MCResult mc = marchingCubes(sdf);
    sdf.data.clear(); // Free SDF memory

    // If MC output is empty, return original mesh
    if (mc.T.empty()) {
        progress("Deep Repair skipped: reconstruction result empty");
        return {std::vector<Vec3>(V), std::vector<Tri>(T), 0};
    }

    std::vector<Vec3> newV = std::move(mc.V);
    std::vector<Tri> newT = std::move(mc.T);

    // Laplacian smoothing
    if (opts.voxelSmoothing && !newV.empty()) {
        progress("Smoothing...");
        newV = laplacianSmooth(newV, newT, opts.voxelSmoothIterations, opts.voxelSmoothLambda);
    }

    // Project to original surface
    if (opts.voxelProjectToSurface && !newV.empty() && !T.empty()) {
        progress("Projecting to original surface...");
        BVH* originalBVH = buildBVH(V, T);
        double maxDist = opts.voxelProjectMaxDist;
        if (maxDist < 0) {
            maxDist = savedVoxelSize * 2.0;
        }
        newV = projectToOriginalSurface(newV, V, T, originalBVH, maxDist);
        delete originalBVH;
    }

    // MC output is structurally manifold, so skip vertex merge / degenerate removal
    // (merging thin shapes would create non-manifold topology)
    // Only remove unused vertices
    progress("Final cleanup...");
    CleanResult cleanResult = removeUnusedVertices(newV, newT);
    newV = std::move(cleanResult.vertices);
    newT = std::move(cleanResult.triangles);

    return {std::move(newV), std::move(newT), resolution};
}
