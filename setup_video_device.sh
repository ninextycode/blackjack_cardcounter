#!/bin/bash

sudo modprobe v4l2loopback video_nr=2 card_label="scrcpy" exclusive_caps=1
