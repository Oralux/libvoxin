#!/bin/bash -xe

# Reference of the text sample:
# The Project Gutenberg EBook of Les Miserables, by Victor Hugo
# This eBook is for the use of anyone anywhere at no cost and with
# almost no restrictions whatsoever.  You may copy it, give it away or
# re-use it under the terms of the Project Gutenberg License included
# with this eBook or online at www.gutenberg.org
# Title: Les Miserables
#        Complete in Five Volumes
# Author: Victor Hugo
# Translator: Isabel F. Hapgood
#Release Date: June 22, 2008 [EBook #135]
#Last Updated: October 30, 2009

unset PLAY
which aplay && PLAY=aplay
which paplay && PLAY=paplay

[ -z "$PLAY" ] && echo "Install aplay (alsa-utils) or paplay (pulseaudio-utils)" && exit 1


getTextFileShort() {
	local TEXTFILE=$1
	cat <<EOF>$TEXTFILE
This arrangement was accepted with absolute submission by Mademoiselle
Baptistine. This holy woman regarded Monseigneur of D---- as at one and
the same time her brother and her bishop, her friend according to the
flesh and her superior according to the Church. She simply loved and
venerated him. When he spoke, she bowed; when he acted, she yielded her
adherence. Their only servant, Madame Magloire, grumbled a little. It
will be observed that Monsieur the Bishop had reserved for himself
only one thousand livres, which, added to the pension of Mademoiselle
Baptistine, made fifteen hundred francs a year. On these fifteen hundred
francs these two old women and the old man subsisted.
EOF
	
}

getTextFileLong() {
	local TEXTFILE=$1
	cat <<EOF>$TEXTFILE
This arrangement was accepted with absolute submission by Mademoiselle
Baptistine. This holy woman regarded Monseigneur of D---- as at one and
the same time her brother and her bishop, her friend according to the
flesh and her superior according to the Church. She simply loved and
venerated him. When he spoke, she bowed; when he acted, she yielded her
adherence. Their only servant, Madame Magloire, grumbled a little. It
will be observed that Monsieur the Bishop had reserved for himself
only one thousand livres, which, added to the pension of Mademoiselle
Baptistine, made fifteen hundred francs a year. On these fifteen hundred
francs these two old women and the old man subsisted.

And when a village curate came to D----, the Bishop still found means to
entertain him, thanks to the severe economy of Madame Magloire, and to
the intelligent administration of Mademoiselle Baptistine.

One day, after he had been in D---- about three months, the Bishop
said:--

"And still I am quite cramped with it all!"

"I should think so!" exclaimed Madame Magloire. "Monseigneur has not
even claimed the allowance which the department owes him for the expense
of his carriage in town, and for his journeys about the diocese. It was
customary for bishops in former days."

"Hold!" cried the Bishop, "you are quite right, Madame Magloire."

And he made his demand.

Some time afterwards the General Council took this demand under
consideration, and voted him an annual sum of three thousand francs,
under this heading: Allowance to M. the Bishop for expenses of carriage,
expenses of posting, and expenses of pastoral visits.

This provoked a great outcry among the local burgesses; and a senator
of the Empire, a former member of the Council of the Five Hundred
which favored the 18 Brumaire, and who was provided with a magnificent
senatorial office in the vicinity of the town of D----, wrote to M.
Bigot de Preameneu, the minister of public worship, a very angry and
confidential note on the subject, from which we extract these authentic
lines:--

"Expenses of carriage? What can be done with it in a town of less than
four thousand inhabitants? Expenses of journeys? What is the use
of these trips, in the first place? Next, how can the posting be
accomplished in these mountainous parts? There are no roads. No one
travels otherwise than on horseback. Even the bridge between Durance and
Chateau-Arnoux can barely support ox-teams. These priests are all thus,
greedy and avaricious. This man played the good priest when he
first came. Now he does like the rest; he must have a carriage and a
posting-chaise, he must have luxuries, like the bishops of the olden
days. Oh, all this priesthood! Things will not go well, M. le Comte,
until the Emperor has freed us from these black-capped rascals. Down
with the Pope! [Matters were getting embroiled with Rome.] For my part,
I am for Caesar alone." Etc., etc.

On the other hand, this affair afforded great delight to Madame
Magloire. "Good," said she to Mademoiselle Baptistine; "Monseigneur
began with other people, but he has had to wind up with himself, after
all. He has regulated all his charities. Now here are three thousand
francs for us! At last!"

That same evening the Bishop wrote out and handed to his sister a
memorandum conceived in the following terms:--

EXPENSES OF CARRIAGE AND CIRCUIT.

  For furnishing meat soup to the patients in the hospital. 1,500 livres
  For the maternity charitable society of Aix . . . . . . .   250   "
  For the maternity charitable society of Draguignan  . . .   250   "
  For foundlings  . . . . . . . . . . . . . . . . . . . . .   500   "
  For orphans   . . . . . . . . . . . . . . . . . . . . . .   500   "
                                                            -----
       Total  . . . . . . . . . . . . . . . . . . . . . . . 3,000   "

Such was M. Myriel's budget.

As for the chance episcopal perquisites, the fees for marriage bans,
dispensations, private baptisms, sermons, benedictions, of churches or
chapels, marriages, etc., the Bishop levied them on the wealthy with all
the more asperity, since he bestowed them on the needy.

After a time, offerings of money flowed in. Those who had and those who
lacked knocked at M. Myriel's door,--the latter in search of the alms
which the former came to deposit. In less than a year the Bishop had
become the treasurer of all benevolence and the cashier of all those
in distress. Considerable sums of money passed through his hands, but
nothing could induce him to make any change whatever in his mode of
life, or add anything superfluous to his bare necessities.

Far from it. As there is always more wretchedness below than there
is brotherhood above, all was given away, so to speak, before it was
received. It was like water on dry soil; no matter how much money he
received, he never had any. Then he stripped himself.

The usage being that bishops shall announce their baptismal names at the
head of their charges and their pastoral letters, the poor people of the
country-side had selected, with a sort of affectionate instinct, among
the names and prenomens of their bishop, that which had a meaning for
them; and they never called him anything except Monseigneur Bienvenu
[Welcome]. We will follow their example, and will also call him thus
when we have occasion to name him. Moreover, this appellation pleased
him.


EOF

}

rawToWAV() {
	local rawfile=$1
	sox -r 11025 -e signed -b 16 -c 1 $rawfile ${rawfile%.*}.wav
}


FILE=$(mktemp -t say.XXXXXXXXXX)

echo "test 1"
time ./say "hello world!" > $FILE.wav 
$PLAY $FILE.wav
rm $FILE.wav

echo "test 2"
getTextFileShort $FILE
time ./say -f $FILE | $PLAY

echo "test 3"
getTextFileLong $FILE
time ./say -j 4 -s 500 -w $FILE.wav -f $FILE
$PLAY $FILE.wav
rm $FILE.wav

echo "test 4"
getTextFileLong $FILE
time ./say -S 86 -f $FILE | $PLAY

rm "$FILE"

# debug
#sudo bash -c "echo 0 > /proc/sys/kernel/yama/ptrace_scope"
# ./say -d > /tmp/fic1
# gdb -p $(pidof say)
# (gdb) up
# (gdb) up
# ...
#		  sleep(5);
# (gdb) set var debug=0
# (gdb) continue

