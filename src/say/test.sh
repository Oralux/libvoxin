#!/bin/bash -xe

# Reference of the text samples:
# Les Misérables, by Victor Hugo, 1862, Tome I, Fantine, chapter II.
# 
# Project Gutenberg licence:
# This eBook is for the use of anyone anywhere at no cost and with
# almost no restrictions whatsoever.  You may copy it, give it away or
# re-use it under the terms of the Project Gutenberg License included
# with this eBook or online at www.gutenberg.org
#
# - in English
# https://www.gutenberg.org/files/135/135-h/135-h.htm
# Translator: Isabel F. Hapgood
#
# - in French
# https://www.gutenberg.org/files/17489/17489-h/17489-h.htm
#

unset PLAY
which aplay && PLAY=aplay
which paplay && PLAY=paplay

[ -z "$PLAY" ] && echo "Install aplay (alsa-utils) or paplay (pulseaudio-utils)" && exit 1


getEnglishText() {
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


getFrenchText() {

	local TEXTFILE=$1
	cat <<EOF>$TEXTFILE
Cet arrangement fut accepté avec une soumission absolue par
mademoiselle Baptistine. Pour cette sainte fille, M. de Digne était
tout à la fois son frère et son évêque, son ami selon la nature et son
supérieur selon l'église. Elle l'aimait et elle le vénérait tout
simplement. Quand il parlait, elle s'inclinait; quand il agissait,
elle adhérait. La servante seule, madame Magloire, murmura un
peu. M. l'évêque, on l'a pu remarquer, ne s'était réservé que mille
livres, ce qui, joint à la pension de mademoiselle Baptistine, faisait
quinze cents francs par an. Avec ces quinze cents francs, ces deux
vieilles femmes et ce vieillard vivaient.

Et quand un curé de village venait à Digne, M. l'évêque trouvait
encore moyen de le traiter, grâce à la sévère économie de madame
Magloire et à l'intelligente administration de mademoiselle
Baptistine.

Un jour—il était à Digne depuis environ trois mois—l'évêque dit:

—Avec tout cela je suis bien gêné!

—Je le crois bien! s'écria madame Magloire, Monseigneur n'a seulement
pas réclamé la rente que le département lui doit pour ses frais de
carrosse en ville et de tournées dans le diocèse. Pour les évêques
d'autrefois c'était l'usage.

—Tiens! dit l'évêque, vous avez raison, madame Magloire.

Il fit sa réclamation.

Quelque temps après, le conseil général, prenant cette demande en
considération, lui vota une somme annuelle de trois mille francs, sous
cette rubrique: Allocation à M. l'évêque pour frais de carrosse, frais
de poste et frais de tournées pastorales.

Cela fit beaucoup crier la bourgeoisie locale, et, à cette occasion,
un sénateur de l'empire, ancien membre du conseil des cinq-cents
favorable au dix-huit brumaire et pourvu près de la ville de Digne
d'une sénatorerie magnifique, écrivit au ministre des cultes, M. Bigot
de Préameneu, un petit billet irrité et confidentiel dont nous
extrayons ces lignes authentiques:

«—Des frais de carrosse? pourquoi faire dans une ville de moins de
quatre mille habitants? Des frais de poste et de tournées? à quoi bon
ces tournées d'abord? ensuite comment courir la poste dans un pays de
montagnes? Il n'y a pas de routes. On ne va qu'à cheval. Le pont même
de la Durance à Château-Arnoux peut à peine porter des charrettes à
bœufs. Ces prêtres sont tous ainsi. Avides et avares. Celui-ci a fait
le bon apôtre en arrivant. Maintenant il fait comme les autres. Il lui
faut carrosse et chaise de poste. Il lui faut du luxe comme aux
anciens évêques. Oh! toute cette prêtraille! Monsieur le comte, les
choses n'iront bien que lorsque l'empereur nous aura délivrés des
calotins. À bas le pape! (les affaires se brouillaient avec
Rome). Quant à moi, je suis pour César tout seul. Etc., etc.»

La chose, en revanche, réjouit fort madame Magloire.

—Bon, dit-elle à mademoiselle Baptistine, Monseigneur a commencé par
les autres, mais il a bien fallu qu'il finît par lui-même. Il a réglé
toutes ses charités. Voilà trois mille livres pour nous. Enfin!

Le soir même, l'évêque écrivit et remit à sa sœur une note ainsi
conçue:

Frais de carrosse et de tournées.

Pour donner du bouillon de viande aux malades de l'hôpital: quinze
cents livres
Pour la société de charité maternelle d'Aix: deux cent cinquante
livres
Pour la société de charité maternelle de Draguignan: deux cent
cinquante livres
Pour les enfants trouvés: cinq cents livres
Pour les orphelins: cinq cents livres

Total: trois mille livres

Tel était le budget de M. Myriel.

Quant au casuel épiscopal, rachats de bans, dispenses, ondoiements,
prédications, bénédictions d'églises ou de chapelles, mariages, etc.,
l'évêque le percevait sur les riches avec d'autant plus d'âpreté qu'il
le donnait aux pauvres.

Au bout de peu de temps, les offrandes d'argent affluèrent. Ceux qui
ont et ceux qui manquent frappaient à la porte de M. Myriel, les uns
venant chercher l'aumône que les autres venaient y déposer. L'évêque,
en moins d'un an, devint le trésorier de tous les bienfaits et le
caissier de toutes les détresses. Des sommes considérables passaient
par ses mains; mais rien ne put faire qu'il changeât quelque chose à
son genre de vie et qu'il ajoutât le moindre superflu à son
nécessaire.

Loin de là. Comme il y a toujours encore plus de misère en bas que de
fraternité en haut, tout était donné, pour ainsi dire, avant d'être
reçu; c'était comme de l'eau sur une terre sèche; il avait beau
recevoir de l'argent, il n'en avait jamais. Alors il se dépouillait.

L'usage étant que les évêques énoncent leurs noms de baptême en tête
de leurs mandements et de leurs lettres pastorales, les pauvres gens
du pays avaient choisi, avec une sorte d'instinct affectueux, dans les
noms et prénoms de l'évêque, celui qui leur présentait un sens, et ils
ne l'appelaient que monseigneur Bienvenu. Nous ferons comme eux, et
nous le nommerons ainsi dans l'occasion. Du reste, cette appellation
lui plaisait.

EOF

}

FILE=$(mktemp -t say.XXXXXXXXXX)
getEnglishText $FILE.en
head $FILE.en > $FILE.en.short

getFrenchText $FILE.fr
head $FILE.fr > $FILE.fr.short

echo "test 1"
time ./say "hello world!" > $FILE.wav 
$PLAY $FILE.wav
rm $FILE.wav

echo "test 2"
time ./say -f $FILE.en.short | $PLAY

echo "test 3"
time ./say -j 4 -s 500 -w $FILE.wav -f $FILE.en.short
$PLAY $FILE.wav
rm $FILE.wav

echo "test 4"
time ./say -S 100 -f $FILE.en | $PLAY

echo "test 5"
./say -L

echo "test 6"
./say -L | grep -qo ",fr,"
if [ "$?" = "0" ]; then
	echo "French voice installed"
fi

rm "$FILE" "$FILE.en" "$FILE.en.short" "$FILE.fr.short"

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

