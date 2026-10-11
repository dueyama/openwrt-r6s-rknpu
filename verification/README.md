# Isolated verification environment

Use a dedicated native AArch64 Linux Docker host with a case-sensitive volume.
The image pins its Ubuntu base digest; APT package versions are resolved at
image creation, so record the image ID and installed package versions. This is
an integration build check, not a bit-for-bit reproduction environment.

```sh
docker build -t r6s-verifier -f verification/Dockerfile .
docker volume create r6s-verifier-work
docker run --rm -it --cpus=4 --memory=7g \
  --mount type=bind,src="$PWD",dst=/repo,readonly \
  --mount type=volume,src=r6s-verifier-work,dst=/work \
  r6s-verifier bash
```

Inside the disposable container, prepare the volume for the unprivileged user,
then build from public source. Do not bind an existing build tree or device.

```sh
chown builder:builder /work
su builder
cd /work
git init openwrt
git -C openwrt remote add origin https://github.com/openwrt/openwrt.git
git -C openwrt fetch --depth=1 origin 66673aad8f99e450f6ee2587254b1b2d19d45150
git -C openwrt checkout --detach FETCH_HEAD
python3 /repo/scripts/stage-openwrt.py --openwrt /work/openwrt
cp /repo/verification/openwrt.config /work/openwrt/.config
cd /work/openwrt
make defconfig
make download -j2
make -j2 V=s
```

This minimal configuration uses in-tree packages and does not install optional
feeds. The shallow checkout produces revision label `r0-66673aa`; fetching full
history changes that label. Retain the resolved `.config`, build logs, compiler versions and artifact
hashes. Building an image does not authorize installing it on a device.

For the optional GNU runtime, mount the pinned source-download directories
read-only and follow [runtime/README.md](../runtime/README.md). Put every source
and output directory on the case-sensitive volume. Run the source checks with
`python3 /repo/scripts/verify.py`; they need no hardware.
