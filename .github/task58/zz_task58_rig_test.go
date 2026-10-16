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
//   names     the held road, for fix/live-row-names's list names and rail density shots.

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/base64"
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

	case "held", "notreadnow", "permanent", "epochheld", "unproven", "absent", "names", "listui":
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

	case "list2", "listrestore", "list3":
		// THE FOUNDERS ROAD (feat/conversation-list): founder 1 answers the app's first join code on the
		// file road, as the held road does; founder 2 answers the fresh one the app publishes after
		// that join, in a group that a filler device joined first, so the app joins it ABOVE epoch 1.
		// Then, on the driver's word (send-g1.go), founder 1 writes into the first group while the app
		// has the second open.
		keyPackagePath := filepath.Join(handshake, "app.keypackage")
		kp1, ok := task58Wait(t, keyPackagePath, stop)
		if !ok {
			return
		}
		os.Remove(keyPackagePath)
		g1 := task58Group(t, ctx, founder)
		invite1, err := g1.AddMember(kp1)
		if err != nil {
			t.Fatalf("RIG G1 AddMember: %v", err)
		}
		if err := g1.Open(ctx); err != nil {
			t.Fatalf("RIG G1 Open: %v", err)
		}
		encoded1 := task58Encode(t, invite1)
		task58Write(t, filepath.Join(rigDir, "g1.txt"), []byte(hex.EncodeToString(g1.Id())))
		task58Write(t, filepath.Join(rigDir, "invite1.bin"), encoded1)
		task58Write(t, filepath.Join(handshake, "app.invite"), encoded1)
		if _, err := g1.Send(ctx, "Line one of the first conversation, from its founder."); err != nil {
			t.Logf("RIG G1 send: %v", err)
		}
		t.Logf("RIG G1 %x: invitation 1 written", g1.Id())

		kp2, ok := task58Wait(t, keyPackagePath, stop)
		if !ok {
			return
		}
		os.Remove(keyPackagePath)
		founder2 := world.device(t, "founder2")
		if err := founder2.Connect(ctx); err != nil {
			t.Fatalf("RIG founder 2's Connect: %v", err)
		}
		filler := world.device(t, "filler")
		if err := filler.Connect(ctx); err != nil {
			t.Fatalf("RIG the filler's Connect: %v", err)
		}
		fillerKeyPackage, err := filler.KeyPackage()
		if err != nil {
			t.Fatalf("RIG the filler's KeyPackage: %v", err)
		}
		g2 := task58Group(t, ctx, founder2)
		fillerInvite, err := g2.AddMember(fillerKeyPackage)
		if err != nil {
			t.Fatalf("RIG G2 AddMember (filler): %v", err)
		}
		if err := g2.Open(ctx); err != nil {
			t.Fatalf("RIG G2 Open: %v", err)
		}
		if _, err := filler.Join(ctx, fillerInvite); err != nil {
			t.Logf("RIG the filler's Join: %v", err)
		}
		invite2, err := g2.AddMemberAndPublish(ctx, kp2)
		if err != nil {
			t.Fatalf("RIG G2 AddMemberAndPublish (the app): %v", err)
		}
		encoded2 := task58Encode(t, invite2)
		task58Write(t, filepath.Join(rigDir, "g2.txt"), []byte(hex.EncodeToString(g2.Id())))
		task58Write(t, filepath.Join(rigDir, "invite2.bin"), encoded2)
		task58Write(t, filepath.Join(handshake, "app.invite"), encoded2)
		if _, err := g2.Send(ctx, "Line one of the second conversation, from its founder."); err != nil {
			t.Logf("RIG G2 send: %v", err)
		}
		task58Write(t, filepath.Join(rigDir, "list-ready"), []byte("ready"))
		t.Logf("RIG G2 %x: invitation 2 written, above epoch 1", g2.Id())

		signal := filepath.Join(rigDir, "send-g1.go")
		// list3's founder 3, who joins the group the app founds
		var founder3 *urmessage.Device
		var g3 *urmessage.Group
		for {
			if _, err := os.Stat(stop); err == nil {
				return
			}
			// list3: founder 3's join code, for the app's add panel
			if _, err := os.Stat(filepath.Join(rigDir, "f3-kp.go")); err == nil && founder3 == nil {
				os.Remove(filepath.Join(rigDir, "f3-kp.go"))
				founder3 = world.device(t, "founder3")
				if err := founder3.Connect(ctx); err != nil {
					t.Fatalf("RIG founder 3's Connect: %v", err)
				}
				kp3, err := founder3.KeyPackage()
				if err != nil {
					t.Fatalf("RIG founder 3's KeyPackage: %v", err)
				}
				task58Write(t, filepath.Join(rigDir, "f3-kp.bin"), kp3)
				t.Logf("RIG founder 3's join code written: %d octets", len(kp3))
			}
			// list3: the invitation the app made for founder 3, as the app shows it (base64)
			if raw, err := os.ReadFile(filepath.Join(rigDir, "f3-invite.b64")); err == nil && founder3 != nil && g3 == nil {
				os.Remove(filepath.Join(rigDir, "f3-invite.b64"))
				octets, err := base64.StdEncoding.DecodeString(string(bytes.TrimSpace(raw)))
				if err != nil {
					t.Fatalf("RIG the app's invitation for founder 3 does not decode: %v", err)
				}
				invite, err := urmessage.ParseInvite(octets)
				if err != nil {
					t.Fatalf("RIG ParseInvite (founder 3): %v", err)
				}
				joined, err := founder3.Join(ctx, invite)
				if err != nil {
					t.Fatalf("RIG founder 3's Join: %v", err)
				}
				g3 = joined
				if _, err := g3.Receive(ctx); err != nil {
					t.Logf("RIG G3 receive after the join: %v", err)
				}
				if _, err := g3.Send(ctx, "Line one of the third conversation, from the one who joined it."); err != nil {
					t.Logf("RIG G3 send: %v", err)
				}
				task58Write(t, filepath.Join(rigDir, "f3-joined.txt"), []byte(hex.EncodeToString(g3.Id())))
				t.Logf("RIG founder 3 joined G3 %x and wrote into it", g3.Id())
			}
			// list3: founder 1 removes the app from G1
			if _, err := os.Stat(filepath.Join(rigDir, "remove-app-g1.go")); err == nil {
				os.Remove(filepath.Join(rigDir, "remove-app-g1.go"))
				if _, err := g1.Receive(ctx); err != nil {
					t.Logf("RIG G1 receive before the removal: %v", err)
				}
				members, err := g1.Members()
				if err != nil {
					t.Fatalf("RIG G1 Members: %v", err)
				}
				removedOne := false
				for _, m := range members {
					if m.Mine {
						continue
					}
					if err := g1.RemoveMember(ctx, m.IdentityPub); err != nil {
						t.Fatalf("RIG G1 RemoveMember: %v", err)
					}
					removedOne = true
					break
				}
				if !removedOne {
					t.Fatalf("RIG G1 holds no member but its founder")
				}
				task58Write(t, filepath.Join(rigDir, "removed-g1.txt"), []byte(time.Now().Format(time.RFC3339Nano)))
				t.Logf("RIG founder 1 removed the app from G1")
			}
			// list3: founder 3 writes into G3 again, after the app's removal from G1
			if _, err := os.Stat(filepath.Join(rigDir, "send-g3.go")); err == nil && g3 != nil {
				os.Remove(filepath.Join(rigDir, "send-g3.go"))
				if _, err := g3.Receive(ctx); err != nil {
					t.Logf("RIG G3 receive before the second line: %v", err)
				}
				if _, err := g3.Send(ctx, "A second line in the third conversation, after the first removed the app."); err != nil {
					t.Logf("RIG G3 second send: %v", err)
				}
				task58Write(t, filepath.Join(rigDir, "sent-g3.txt"), []byte(time.Now().Format(time.RFC3339Nano)))
				t.Logf("RIG G3: the second line is sent")
			}
			if _, err := os.Stat(signal); err == nil {
				os.Remove(signal)
				if _, err := g1.Receive(ctx); err != nil {
					t.Logf("RIG G1 receive before the second line: %v", err)
				}
				if _, err := g1.Send(ctx, "A second line in the first conversation, while the second is open."); err != nil {
					t.Logf("RIG G1 second send: %v", err)
				}
				task58Write(t, filepath.Join(rigDir, "sent-g1.txt"), []byte(time.Now().Format(time.RFC3339Nano)))
				t.Logf("RIG G1: the second line is sent")
			}
			time.Sleep(300 * time.Millisecond)
		}

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
