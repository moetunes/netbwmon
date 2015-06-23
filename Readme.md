# NETBWMON

Network monitor based on [nbwmon](https://github.com/causes-/nbwmon) and 
[netmon](https://github.com/vurtun/netmon) but
without Ncurses dependency and only Linux support.

```
$ netbwmon -s -q -d 0.5 -i wlan0
```

![network monitor](/screen/screen.png?raw=true)

## Usage
```
Usage: netbwmon [options]

Options:

    -h,         output help information
    -v,         output application version
    -c,         disable graph colors
    -s,         use SI units
    -d <sec>,   redraw delay
    -i <int>,   network interface
    -q          quiet <don't show stats>
```

## Features

    - bandwidth graph
    - current, average and maximum transfer speed
    - total traffic
    - window scaling
    - supports multiple units
    - 256 color support

## Keys

    - q .. quit
    - d .. increase update time + 0.5 sec
    - D .. decrease update time - 0.5 sec
    - v .. toggle interface name, average and total stats

# License
(The MIT License)

