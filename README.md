# chinook
a http/1.1 server written in c
## getting started
how to get the server running
### before you start - requirements
- macos or linux (for linux or intel macbook you wil need to compile the binaries yourself)
### installation
#### macOS arm
1. find the latest release and download the binary.
2. cd to where the binary is downloaded
```
cd ~/Downloads
```
3. add the binary to the path
```
sudo ln -s chinook /usr/local/bin/chinook
```
4. check it works
```
chinook help
```
#### Linux or Intel Mac
1. download the source code
2. compile the code with cmake and make
```
cmake -B build
cd build
make
```
4. add the binary to the path
```
sudo ln -s chinook <put in the your path for linux>
```
5. check it works
```
chinook help
```
### running the server
run the server with
```
chinook start
```
this will create a background process, to stop it, enter
```
chinook stop
```
by default chinook will host a default website on localhost port 8080. go to http://127.0.0.1:8080 and check the default page
### setting up the config
on macos chinook will use ~/chinook_config.json as the default config path. all config settings are provided below (as default values)
```json
{
    "server": {
        "port": 80,
        "address": "127.0.0.1", // 0.0.0.0 to bind to all addresses
        "protocol": "ipv4", // or ipv6
        "backlog": 100,
        "keep-aive-timeout": 60 // seconds
    }
}
```
## other stuff
### License
### Acknowledgements
me, idk i just folllowed some template and there was acknolwedemgnets
