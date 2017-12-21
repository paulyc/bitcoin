# DO NOT EDIT
# This makefile makes sure all linkable targets are
# up-to-date with anything they link to
default:
	echo "Do not invoke directly"

# Rules to remove targets that are older than anything to which they
# link.  This forces Xcode to relink the targets from scratch.  It
# does not seem to check these dependencies itself.
PostBuild.bitcoin.Debug:
/Users/paulyc/Development/bitcoin/xcode2/Debug/bitcoin:
	/bin/rm -f /Users/paulyc/Development/bitcoin/xcode2/Debug/bitcoin


PostBuild.bitcoin.Release:
/Users/paulyc/Development/bitcoin/xcode2/Release/bitcoin:
	/bin/rm -f /Users/paulyc/Development/bitcoin/xcode2/Release/bitcoin


PostBuild.bitcoin.MinSizeRel:
/Users/paulyc/Development/bitcoin/xcode2/MinSizeRel/bitcoin:
	/bin/rm -f /Users/paulyc/Development/bitcoin/xcode2/MinSizeRel/bitcoin


PostBuild.bitcoin.RelWithDebInfo:
/Users/paulyc/Development/bitcoin/xcode2/RelWithDebInfo/bitcoin:
	/bin/rm -f /Users/paulyc/Development/bitcoin/xcode2/RelWithDebInfo/bitcoin




# For each target create a dummy ruleso the target does not have to exist
