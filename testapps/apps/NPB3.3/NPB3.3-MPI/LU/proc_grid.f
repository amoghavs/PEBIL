
c---------------------------------------------------------------------
c---------------------------------------------------------------------

      subroutine proc_grid

c---------------------------------------------------------------------
c---------------------------------------------------------------------

      implicit none

      include 'applu.incl'

c---------------------------------------------------------------------
c  local variables
c---------------------------------------------------------------------

c---------------------------------------------------------------------
c
c   set up a two-d grid for processors: column-major ordering of unknowns
c   NOTE: assumes a power-of-two number of processors
c
c---------------------------------------------------------------------

      xdim   = 2**(ndim/2)
      if (mod(ndim,2).eq.1) xdim = xdim + xdim
      ydim   = num/xdim

      row    = mod(id,xdim) + 1
      col    = id/xdim + 1


      return
      end


