# ALPS_SIF

image online: [alps](https://cloud.sylabs.io/library/qiyang/alps/alps)

**ALPS** is the (Algorithms and Libraries for Physics Simulations) project. Related to [ALPSCore](https://github.com/ALPSCore/ALPSCore).

This repo contains singularity def file and old version source file to produce a **MPI-support**, **compiled** singularity image. Easy to follow and include.

Fully compiled from source code.

You may need to modify the available source of Ubuntu to speed up in ```main.def```

You may need to modify the first line of ```main.def``` : PARALLEL_BUILD=32 .
