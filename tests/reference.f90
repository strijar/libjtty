! Optional interoperability oracle. Links the original WSJT-X Fortran only
! into this test executable, never into libjtty.
program reference
  use iso_fortran_env, only: int64
  use jtty_tbcc_code_profiles
  use tbcc, only: tbcc_encode
  implicit none
  character(len=256) :: arg,path
  integer(int64) :: word
  integer :: payload(34),tones(59),i,unit,nwave
  integer, parameter :: n=59*384
  real :: wave(n)
  complex :: dummy(1)
  call get_command_argument(1,arg)
  call get_command_argument(2,path)
  read(arg,'(z16)') word
  do i=1,34
    payload(i)=int(ibits(word,34-i,1))
  enddo
  tones(1:13)=[0,2,2,3,0,0,3,2,1,3,1,2,0]
  call tbcc_encode(payload,tones(14:59),JTTY_TBCC_PROFILE_1167_1545_80F)
  write(*,'(59i1)') tones
  nwave=n
  call gen_jttywave(tones,59,384,2.0,12000.0,1000.0,dummy,wave,0,nwave)
  open(newunit=unit,file=trim(path),access='stream',form='unformatted',status='replace')
  write(unit) wave
  close(unit)
end program reference
