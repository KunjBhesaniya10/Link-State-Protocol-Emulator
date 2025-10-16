!bin/bash

## run VN.cpp 5 times simultaneously open each in a new terminal window and close the terminal window after execution
trap "pkill -P $$" EXIT
for i in {1..5}
do
  gnome-terminal -- bash -c "g++ VN.cpp -o VN && ./VN; exec bash"
done
## add code such that if i kill this process all the terminal windows are also closed