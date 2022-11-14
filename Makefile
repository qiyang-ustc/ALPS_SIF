launch: alpine
	sudo singularity exec --writable alpine sh

alpine: alpine.sif
	sudo singularity build --sandbox alpine alpine.sif

alpine.sif: main.def
	sudo singularity build alpine.sif main.def 

clear:
	sudo rm -rf alpine alpine.sif