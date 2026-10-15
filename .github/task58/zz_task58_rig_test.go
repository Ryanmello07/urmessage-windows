package cp3b

// THROWAWAY, AND NEVER MERGED: message-windows branch ci/task58-shots on the owner's fork copies
// this file into urnetwork/message's sdk/cp3b on a GitHub runner, at the T1 branch's head, to give
// the Windows app a message server and the other parties hardening plan Task 58's screenshots
// need. It is never committed to urnetwork/message.
//
// The server is the real pipeline (peer + api + a memory store) behind message-server's own
// endpoint, on a loopback port with a self-signed key the app pins: newEndpointWorld, exactly as
// endpointroute_test.go drives it. Every party is a urmessage.Device over that endpoint. What each
// leg needs (TASK58_LEG):
//
//   joiner    the app's join code arrives as rigDir/app.kp. The rig answers with one invitation per
//             refusal the app is to be shown, each a file in rigDir: other.bin (made for another
//             device), flipped.bin (one bit of its authenticator flipped), spent.bin (an honest
//             one, whose join the driver makes cost its join code) and, from the pre-T1 rig's
//             group, predates.bin (that group's Welcome under a v3 envelope). Then joiner-ready.
//   adder     the app founds a group and adds. The rig writes the join codes to paste:
//             damaged.bin (an honest one with one bit of its signature flipped), honest.bin, and
//             second.bin (a second join code of the same device). Then adder-ready.
//   held      Task 39's road: the first join code the app publishes is answered by the file road,
//             so the app joins and holds the group; invite1.bin keeps the invitation.
//   notreadnow, permanent, epochheld and unproven: the same as held (ci/task58-shots-v3 to -v5);
//             the driver then holds, damages or denies the group's files on the app's disk.
//   nowrapkey nothing but the server.

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/hex"
	"fmt"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/urnetwork/message/sdk/urmessage"
)

func task58Wait(t *testing.T, path string, stop string) ([]byte, bool) {
	t.Helper()
	for {
		if _, err := os.Stat(stop); err == nil {
			return nil, false
		}
		first, err := os.ReadFile(path)
		if err == nil && 0 < len(first) {
			time.Sleep(300 * time.Millisecond)
			second, err := os.ReadFile(path)
			if err == nil && bytes.Equal(first, second) {
				return second, true
			}
		}
		time.Sleep(500 * time.Millisecond)
	}
}

func task58Write(t *testing.T, path string, content []byte) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0o700); err != nil {
		t.Fatalf("RIG %s: %v", path, err)
	}
	temp := path + ".partial"
	if err := os.WriteFile(temp, content, 0o600); err != nil {
		t.Fatalf("RIG %s: %v", path, err)
	}
	if err := os.Rename(temp, path); err != nil {
		t.Fatalf("RIG %s: %v", path, err)
	}
}

func task58Group(t *testing.T, ctx context.Context, founder *urmessage.Device) *urmessage.Group {
	t.Helper()
	groupId := make([]byte, urmessage.GroupIdBytes)
	if _, err := rand.Read(groupId); err != nil {
		t.Fatalf("RIG group id: %v", err)
	}
	group, err := founder.CreateGroup(ctx, groupId)
	if err != nil {
		t.Fatalf("RIG CreateGroup: %v", err)
	}
	return group
}

func task58Encode(t *testing.T, invite *urmessage.Invite) []byte {
	t.Helper()
	encoded, err := invite.Encode()
	if err != nil {
		t.Fatalf("RIG encode: %v", err)
	}
	return encoded
}

func TestTask58Rig(t *testing.T) {
	rigDir := os.Getenv("TASK58_RIG_DIR")
	handshake := os.Getenv("TASK58_HANDSHAKE_DIR")
	leg := os.Getenv("TASK58_LEG")
	if rigDir == "" || handshake == "" || leg == "" {
		t.Skip("TASK58_RIG_DIR, TASK58_HANDSHAKE_DIR and TASK58_LEG are not set: this is the Task 58 runner rig")
	}
	ctx := context.Background()
	stop := filepath.Join(rigDir, "stop")

	world := newEndpointWorld(t)
	founder := world.device(t, "founder")
	if err := founder.Connect(ctx); err != nil {
		t.Fatalf("RIG the founder's Connect: %v", err)
	}
	task58Write(t, filepath.Join(rigDir, "endpoint.txt"), []byte(fmt.Sprintf("%s\n%s\n", world.url, hex.EncodeToString(world.pin))))
	t.Logf("RIG leg %s serving %s, pin %s; founder connected", leg, world.url, hex.EncodeToString(world.pin))

	switch leg {
	case "joiner":
		appKeyPackage, ok := task58Wait(t, filepath.Join(rigDir, "app.kp"), stop)
		if !ok {
			return
		}
		t.Logf("RIG the app's join code: %d octets", len(appKeyPackage))

		// MADE FOR ANOTHER DEVICE: a join code of a device that is not the app
		other := world.device(t, "other")
		otherKeyPackage, err := other.KeyPackage()
		if err != nil {
			t.Fatalf("RIG the other device's KeyPackage: %v", err)
		}
		otherInvite, err := task58Group(t, ctx, founder).AddMember(otherKeyPackage)
		if err != nil {
			t.Fatalf("RIG AddMember (other): %v", err)
		}
		task58Write(t, filepath.Join(rigDir, "other.bin"), task58Encode(t, otherInvite))

		// ONE BIT OF THE AUTHENTICATOR FLIPPED, under a checksum that covers the change: Encode
		// recomputes it, as a carrier that changed the octets would
		flippedGroup := task58Group(t, ctx, founder)
		honestInvite, err := flippedGroup.AddMember(appKeyPackage)
		if err != nil {
			t.Fatalf("RIG AddMember (flipped): %v", err)
		}
		seal := append([]byte(nil), honestInvite.JoinSeal...)
		auth := append([]byte(nil), honestInvite.JoinAuth...)
		flipped := *honestInvite
		flipped.JoinAuth = append([]byte(nil), honestInvite.JoinAuth...)
		flipped.JoinAuth[0] ^= 0x01
		task58Write(t, filepath.Join(rigDir, "flipped.bin"), task58Encode(t, &flipped))

		// AN HONEST INVITATION, whose join the driver makes cost its join code
		spentGroup := task58Group(t, ctx, founder)
		spentInvite, err := spentGroup.AddMember(appKeyPackage)
		if err != nil {
			t.Fatalf("RIG AddMember (spent): %v", err)
		}
		task58Write(t, filepath.Join(rigDir, "spent-gid.txt"), []byte(hex.EncodeToString(spentGroup.Id())))
		task58Write(t, filepath.Join(rigDir, "spent.bin"), task58Encode(t, spentInvite))

		// A GROUP THE PRE-T1 BUILD FOUNDED: its Welcome and tree, answered to the app's join code by
		// that build (zz_task58_old_rig_test.go), carried in a version 3 envelope with a seal of the
		// right shape. The join opens the Welcome and is refused for what the group is.
		// (the older build may refuse the app's join code; it then writes old-refused.txt and the
		// driver says the predates row was not reached)
		for {
			_, gidErr := os.Stat(filepath.Join(rigDir, "old-gid.bin"))
			_, refusedErr := os.Stat(filepath.Join(rigDir, "old-refused.txt"))
			_, stopErr := os.Stat(stop)
			if gidErr == nil || refusedErr == nil || stopErr == nil {
				break
			}
			time.Sleep(500 * time.Millisecond)
		}
		if _, err := os.Stat(filepath.Join(rigDir, "old-refused.txt")); err == nil {
			t.Logf("RIG the older build refused the app's join code; no predates invitation")
		} else if oldGid, ok := task58Wait(t, filepath.Join(rigDir, "old-gid.bin"), stop); ok {
			oldWelcome, _ := task58Wait(t, filepath.Join(rigDir, "old-welcome.bin"), stop)
			oldTree, _ := task58Wait(t, filepath.Join(rigDir, "old-tree.bin"), stop)
			predates := &urmessage.Invite{GroupId: oldGid, Welcome: oldWelcome, RatchetTree: oldTree, JoinSeal: seal, JoinAuth: auth}
			if encoded, err := predates.Encode(); err != nil {
				t.Logf("RIG the predates envelope would not encode: %v", err)
			} else {
				task58Write(t, filepath.Join(rigDir, "predates.bin"), encoded)
			}
		}
		task58Write(t, filepath.Join(rigDir, "joiner-ready"), []byte("ready"))
		t.Logf("RIG joiner invitations written")

	case "adder":
		joiner := world.device(t, "joiner")
		honest, err := joiner.KeyPackage()
		if err != nil {
			t.Fatalf("RIG KeyPackage: %v", err)
		}
		second, err := joiner.KeyPackage()
		if err != nil {
			t.Fatalf("RIG the second KeyPackage: %v", err)
		}
		damaged := append([]byte(nil), honest...)
		damaged[len(damaged)-1] ^= 0x01 // the last octet of its signature
		task58Write(t, filepath.Join(rigDir, "damaged.bin"), damaged)
		task58Write(t, filepath.Join(rigDir, "honest.bin"), honest)
		task58Write(t, filepath.Join(rigDir, "second.bin"), second)
		task58Write(t, filepath.Join(rigDir, "adder-ready"), []byte("ready"))
		t.Logf("RIG adder join codes written: %d, %d octets", len(honest), len(second))

	case "held", "notreadnow", "permanent", "epochheld", "unproven", "absent":
		keyPackagePath := filepath.Join(handshake, "app.keypackage")
		keyPackage, ok := task58Wait(t, keyPackagePath, stop)
		if !ok {
			return
		}
		os.Remove(keyPackagePath)
		group := task58Group(t, ctx, founder)
		invite, err := group.AddMember(keyPackage)
		if err != nil {
			t.Fatalf("RIG AddMember: %v", err)
		}
		if err := group.Open(ctx); err != nil {
			t.Fatalf("RIG Open: %v", err)
		}
		encoded := task58Encode(t, invite)
		task58Write(t, filepath.Join(rigDir, "gid.txt"), []byte(hex.EncodeToString(group.Id())))
		task58Write(t, filepath.Join(rigDir, "invite1.bin"), encoded)
		task58Write(t, filepath.Join(handshake, "app.invite"), encoded)
		if _, err := group.Send(ctx, "From the runner's founder: invitation 1 is out."); err != nil {
			t.Logf("RIG send: %v", err)
		}
		t.Logf("RIG invitation 1 into group %x written", group.Id())

	case "nowrapkey":
	default:
		t.Fatalf("RIG no such leg %q", leg)
	}

	for {
		if _, err := os.Stat(stop); err == nil {
			t.Logf("RIG stop file seen")
			return
		}
		time.Sleep(time.Second)
	}
}
