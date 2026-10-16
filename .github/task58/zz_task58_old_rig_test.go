package cp3b

// THROWAWAY, AND NEVER MERGED: message-windows branch ci/task58-shots on the owner's fork copies
// this file into urnetwork/message's sdk/cp3b on a GitHub runner, at 215a0696 (message main before
// the join seal, the commit message-windows main pins), to stand in for "an older URmessage" in
// hardening plan Task 58's screenshots. It is never committed to urnetwork/message.
//
// It runs its OWN loopback world (newEndpointWorld at that commit), which the app never connects
// to: nothing it makes is published anywhere the app reads. What it writes into rigDir:
//
//   old-kp.bin                         a join code this build makes (it lists no join seal
//                                      capability: the adder's ErrJoinerCannotOpenSealedInvite)
//   old-v2.bin                         when rigDir/app.kp arrives: an invitation this build makes to
//                                      the app's join code (version 2: ErrInviteVersion)
//   old-gid/welcome/tree.bin           that invitation's group id, Welcome and ratchet tree, which
//                                      the T1 rig carries in a version 3 envelope
//                                      (ErrGroupPredatesJoinSeal at the join)

import (
	"bytes"
	"context"
	"crypto/rand"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func task58OldWrite(t *testing.T, path string, content []byte) {
	t.Helper()
	temp := path + ".partial"
	if err := os.WriteFile(temp, content, 0o600); err != nil {
		t.Fatalf("OLD RIG %s: %v", path, err)
	}
	if err := os.Rename(temp, path); err != nil {
		t.Fatalf("OLD RIG %s: %v", path, err)
	}
}

func TestTask58OldRig(t *testing.T) {
	rigDir := os.Getenv("TASK58_RIG_DIR")
	if rigDir == "" {
		t.Skip("TASK58_RIG_DIR is not set: this is the Task 58 runner's older build")
	}
	ctx := context.Background()
	stop := filepath.Join(rigDir, "stop")
	world := newEndpointWorld(t)

	joiner := world.device(t, "old-joiner")
	keyPackage, err := joiner.KeyPackage()
	if err != nil {
		t.Fatalf("OLD RIG KeyPackage: %v", err)
	}
	task58OldWrite(t, filepath.Join(rigDir, "old-kp.bin"), keyPackage)
	t.Logf("OLD RIG a join code of this build: %d octets", len(keyPackage))

	appKeyPackagePath := filepath.Join(rigDir, "app.kp")
	served := false
	for {
		if _, err := os.Stat(stop); err == nil {
			t.Logf("OLD RIG stop file seen")
			return
		}
		if !served {
			first, err := os.ReadFile(appKeyPackagePath)
			if err == nil && 0 < len(first) {
				time.Sleep(300 * time.Millisecond)
				second, err := os.ReadFile(appKeyPackagePath)
				if err == nil && bytes.Equal(first, second) {
					served = true
					founder := world.device(t, "old-founder")
					if err := founder.Connect(ctx); err != nil {
						t.Fatalf("OLD RIG Connect: %v", err)
					}
					groupId := make([]byte, 32)
					if _, err := rand.Read(groupId); err != nil {
						t.Fatalf("OLD RIG group id: %v", err)
					}
					group, err := founder.CreateGroup(ctx, groupId)
					if err != nil {
						t.Fatalf("OLD RIG CreateGroup: %v", err)
					}
					invite, err := group.AddMember(second)
					if err != nil {
						// reported, not fatal: the T1 rig and the driver then say this row was not reached
						t.Logf("OLD RIG AddMember of the app's join code refused: %v", err)
						task58OldWrite(t, filepath.Join(rigDir, "old-refused.txt"), []byte(err.Error()))
						continue
					}
					encoded, err := invite.Encode()
					if err != nil {
						t.Fatalf("OLD RIG Encode: %v", err)
					}
					task58OldWrite(t, filepath.Join(rigDir, "old-v2.bin"), encoded)
					task58OldWrite(t, filepath.Join(rigDir, "old-welcome.bin"), invite.Welcome)
					task58OldWrite(t, filepath.Join(rigDir, "old-tree.bin"), invite.RatchetTree)
					task58OldWrite(t, filepath.Join(rigDir, "old-gid.bin"), invite.GroupId)
					t.Logf("OLD RIG an invitation of this build to the app's join code: %d octets, version %#04x",
						len(encoded), uint16(encoded[0])<<8|uint16(encoded[1]))
				}
			}
		}
		time.Sleep(500 * time.Millisecond)
	}
}
