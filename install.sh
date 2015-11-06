DLKM=byd-0.1
KERN=$(uname -r)
echo "MAIN: Removing previous versions of psmouse-byd..."
sudo dkms remove psmouse/$DLKM --all
echo "MAIN: Building current driver from source files..."
sudo dkms build psmouse/$DLKM
if [ $? -eq 0 ]; then
        echo "MAIN: Installing the driver"
        sudo dkms install psmouse/$DLKM
        sudo rmmod -v psmouse
        sudo modprobe -v psmouse
else
        printf "Build failed\n"
        cat /var/lib/dkms/psmouse/$DLKM/build/make.log
fi
