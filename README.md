# sbull_ftl_box_woojincho
Porting FTL_Box to Sbull

0. Build Setting
   * `Ubuntu 16.04`
   * `Kernel `

1. Build modules
   * `make`

2. Start FIO (Benchmark)
   * `sudo fio --filename=`$sbullPATH` --name sbull --direct=1 --rw=randwrite --bs=512 --size=128M --numjobs=1 --norandommap`
