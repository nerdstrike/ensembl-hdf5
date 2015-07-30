use strict;
use warnings;

use Test::More;
use Bio::EnsEMBL::HDF5::ArrayAdaptor;
use File::Temp qw/tempfile/;

# For chatty output
#Bio::EnsEMBL::HDF5::set_log(1);

my ($fh, $filename) = tempfile();
print "Creating file\n";
my $aa = new Bio::EnsEMBL::HDF5::ArrayAdaptor($filename, {gene => ['A','B'], snp => ['rs1','rs2']});

my $dim_labels = Bio::EnsEMBL::HDF5::get_dim_labels($aa->{hdf5});

my $original_data = [
  {gene => 'A', snp => 'rs1', value=>.1},
  {gene => 'B', snp => 'rs2', value=>.2}
];

print "Storing data\n";
$aa->store($original_data);

print "Fetching all data\n";
my @output_data = @{$aa->fetch()};

ok(scalar(@output_data) == 2);

foreach my $data_point (@output_data) {
  ok(defined $data_point->{gene});
  ok(defined $data_point->{snp});
  ok(defined $data_point->{value});
  if ($data_point->{gene} eq 'A') {
      ok($data_point->{snp} eq 'rs1');
      ok($data_point->{value} == .1);
    } elsif ($data_point->{gene} eq 'B') {
      ok($data_point->{snp} eq 'rs2');
      ok($data_point->{value} == .2);
    }
}

print "Fetching selected data\n";
@output_data = @{$aa->fetch({gene => 'A'})};

ok(scalar(@output_data) == 1);
my $data_point = pop @output_data;
ok(!defined $data_point->{gene});
ok(defined $data_point->{snp});
ok(defined $data_point->{value});
ok($data_point->{snp} eq 'rs1');
ok($data_point->{value} == .1);

# Test whether an error is raised when an unkown gene is requested
ok(eval {$aa->fetch({gene => 'C'}); 0;} || 1);

done_testing;

unlink $filename;
unlink $filename.".sqlite3";

# Little convenience function for debugging
sub print_dataset {
  my $data_set = shift;
  print "{\n";
  foreach my $data_point (@$data_set) {
      foreach my $key (keys %$data_point) {
	  print "\t$key:$data_point->{$key}"; 
	}  
      print "\n";
    }
  print "}\n";
}
