./qemu/build/qemu-system-x86_64 \
  -m 8G \
  -smp 8 \
  -enable-kvm \
  -drive if=virtio,file=base.qcow2,cache=none \
  -net nic -net user,hostfwd=tcp::2222-:22 \
  -qmp tcp:localhost:4444,server,wait=off \
  -display none \
  # -boot d \
  # -cdrom *.iso \

echo "Press Ctrl+C to stop."

sleep infinity