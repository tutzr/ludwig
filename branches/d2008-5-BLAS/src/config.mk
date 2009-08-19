
# apply appropriate compiler settings, may have to use 'gmake' if 'make' fails (HPCx stringency issues)
# 
###########################################################################
#choose appropriate HPC Machine.
#

#MACHINE = HPCX
MACHINE = Ness
#MACHINE = HecToR
#MACHINE = ECDF


# uncomment to use 'single fluid' scheme, comment to use  'binary fluid' scheme
#SCHEME= single

# uncomment to use to goto blas, comment to use vendor. 
# (goto version requires compiled goto blas library, see  http://www.tacc.utexas.edu/research-development/tacc-projects/)

#GOTO = goto
###########################################################################

ifeq ($(MACHINE),HPCX)
	CC=xlc_r
	MPICC=mpcc_r
	OPTS = -D_D3Q19_ 
	CFLAGS=$(OPTS) -q64 -O5 -qipa -DNDEBUG
	LIBS= -lessl -qipa -lm -DPOWER_ESSL
else
	ifeq ($(MACHINE),Ness)
		CC=gcc
		MPICC=mpicc 

		OPTS= -D_D3Q19_ -fastsse
		CFLAGS=$(OPTS) -DNDEBUG -Minform=warn -Msafeptr -Mipa=inline,fast -DX86
		LIBS=  -lm -lacml -lpgftnrtl -lrt 
	else
		ifeq ($(MACHINE),HecToR)
			CC=cc	
                        MPICC=cc
                        OPTS = -D_D3Q19_ -DNDEBUG
                        CFLAGS=$(OPTS) -O3 -OPT:Ofast -OPT:recip=ON -OPT:malloc_algorithm=1 -inline \
			-INLINE:preempt=ON -march=auto -m64 -msse3 -LNO:simd=2 -DX86
                        LIBS=   -lacml -lm

		else
			ifeq ($(MACHINE), ECDF)
				CC=gcc
				MPICC=mpicc
				OPTS = -D_D3Q19_ -fast -axS -funroll-loops -ansi-alias -align -ipo -vec-report
				CFLAGS=$(OPTS) -DNDEBUG -DX86
				LIBS= -DMKL -L/exports/applications/apps/intel/mkl/10.0.1.014/lib/em64t/ \
				 -lmkl_intel_lp64  -lmkl_sequential -lmkl_core  -lm
			else
				echo	
				echo "OS not defined !!" 
				echo
			endif
		endif
	endif
endif

ifeq ($(GOTO),goto)
        ifeq ($(MACHINE),Ness)
                LIBS=  -lm -L./ -lgoto
        endif
        ifeq ($(MACHINE),ECDF)

                LIBS= -lm -L./ -lgoto_core2p-r1.26
        endif
        ifeq ($(MACHINE),HPCX)
                LIBS= -lm libgoto_power5-r1.26.a -bmap:map -DPOWER_GOTO
        endif
        ifeq ($(MACHINE), HecToR)
                LIBS= -lm -L./ -lgoto_barcelonap-r1.26
        endif
endif


ifeq ($(SCHEME), single)
	OPTS += -D_SINGLE_FLUID_
else
	OPTS += -D_BINARY_FLUID_
endif

###########################################################################
